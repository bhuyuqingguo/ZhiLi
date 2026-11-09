#include <QtWidgets>
#include <QtSvg/QSvgGenerator>
#include <functional>
#include <algorithm>

struct Node {
    QString id, text;
    QStringList children;
    bool collapsed = false;
};

class Canvas : public QGraphicsView {
public:
    explicit Canvas(QGraphicsScene *scene, QWidget *parent=nullptr): QGraphicsView(scene,parent) {
        setRenderHint(QPainter::Antialiasing); setDragMode(QGraphicsView::ScrollHandDrag);
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    }
protected:
    void wheelEvent(QWheelEvent *e) override {
        if (e->modifiers() & Qt::ControlModifier) { double factor=e->angleDelta().y()>0?1.12:1.0/1.12;
            const double s=transform().m11()*factor;
            if(s>=0.2 && s<=3.5) scale(factor,factor);
            e->accept(); return;
        }
        QGraphicsView::wheelEvent(e);
    }
};

class Window : public QMainWindow {
    QGraphicsScene *scene;
    Canvas *canvas;
    QMap<QString,Node> nodes;
    QString root, selected, filePath;
    bool mindMap=true, dirty=false, rendering=false;
    QVector<QByteArray> undoStack, redoStack;
    QMap<QString,QPointF> positions;
    QMap<QString,QGraphicsRectItem*> graphics;
public:
    Window(){
        setWindowTitle(QStringLiteral("SimpleMap — 未命名")); resize(1160,760);
        scene=new QGraphicsScene(this); scene->setBackgroundBrush(QColor("#f7f9fc"));
        canvas=new Canvas(scene,this); setCentralWidget(canvas);
        makeMenus(); resetDocument(); statusBar()->showMessage(QStringLiteral("Ctrl+滚轮缩放 · 拖动空白区域平移 · 双击节点编辑"));
    }
private:
    QString newId() const {return QUuid::createUuid().toString(QUuid::WithoutBraces);}
    QJsonObject document() const {
        QJsonObject o; o["format"]="simplemap-json"; o["version"]=1; o["root"]=root;
        QJsonArray a;
        for(auto it=nodes.constBegin();it!=nodes.constEnd();++it){
            QJsonObject n; n["id"]=it.value().id;n["text"]=it.value().text;n["collapsed"]=it.value().collapsed;
            QJsonArray c; for(const QString &id:it.value().children)c.append(id);n["children"]=c;a.append(n);
        }
        o["nodes"]=a; o["layout"]=mindMap?"mindmap":"organization";return o;
    }
    QByteArray snapshot() const {return QJsonDocument(document()).toJson(QJsonDocument::Compact);}
    bool apply(const QByteArray &bytes,QString *error=nullptr){
        QJsonParseError pe; QJsonDocument d=QJsonDocument::fromJson(bytes,&pe);
        if(pe.error!=QJsonParseError::NoError || !d.isObject()) {if(error)*error=QStringLiteral("JSON 文件解析失败");return false;}
        QJsonObject o=d.object();if(o["format"].toString()!="simplemap-json" || o["version"].toInt()!=1){if(error)*error=QStringLiteral("不是受支持的 SimpleMap JSON v1 文件");return false;}
        QMap<QString,Node> next;QJsonArray arr=o["nodes"].toArray();
        if(arr.size()<1 || arr.size()>100000){if(error)*error=QStringLiteral("节点数量不合法");return false;}
        for(const auto &v:arr){if(!v.isObject())return false;QJsonObject obj=v.toObject();Node n;
            n.id=obj["id"].toString(); n.text=obj["text"].toString();n.collapsed=obj["collapsed"].toBool();
            if(n.id.isEmpty()||next.contains(n.id)||n.text.size()>100000)return false;
            for(const auto &c:obj["children"].toArray()) {if(!c.isString())return false;n.children<<c.toString();}
            next.insert(n.id,n);
        }
        QString nextRoot=o["root"].toString();if(!next.contains(nextRoot))return false;
        QSet<QString> visited, active;
        std::function<bool(QString)> valid=[&](QString id){
            if(!next.contains(id)||active.contains(id)||visited.contains(id))return false;
            active.insert(id);visited.insert(id);
            for(const QString &c:next[id].children)if(!valid(c))return false;
            active.remove(id);return true;
        };
        if(!valid(nextRoot)||visited.size()!=next.size()){if(error)*error=QStringLiteral("导图结构存在环、重复引用或孤立节点");return false;}
        nodes=next;root=nextRoot;mindMap=o["layout"].toString()!="organization";selected=root;
        render();return true;
    }
    void checkpoint(){undoStack.append(snapshot());if(undoStack.size()>80)undoStack.remove(0);redoStack.clear();dirty=true;}
    void history(bool backwards){auto &from=backwards?undoStack:redoStack;auto &to=backwards?redoStack:undoStack;if(from.isEmpty())return;
        QByteArray prior=from.takeLast(), current=snapshot();if(apply(prior)){to.append(current);dirty=true;updateTitle();}}
    void resetDocument(){nodes.clear();Node n; n.id=newId();n.text=QStringLiteral("中心主题");root=n.id;nodes[n.id]=n;
        selected=root;filePath.clear();dirty=false;undoStack.clear();redoStack.clear();mindMap=true;render();updateTitle();}
    void updateTitle(){setWindowTitle(QString("SimpleMap — %1%2").arg(filePath.isEmpty()?QStringLiteral("未命名"):QFileInfo(filePath).fileName(),dirty?" *":""));}
    bool confirmDiscard(){if(!dirty)return true;
        auto r=QMessageBox::question(this,QStringLiteral("未保存修改"),QStringLiteral("当前导图有修改，是否放弃？"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);return r==QMessageBox::Yes;}
    void createNode(bool child){if(selected.isEmpty() || !nodes.contains(selected))selected=root;
        QString parent=selected;
        if(!child){if(selected==root)return;parent=findParent(selected);}
        if(parent.isEmpty())return;
        checkpoint();Node n;n.id=newId();n.text=QStringLiteral("新主题");nodes[n.id]=n;
        if(child){nodes[parent].collapsed=false;nodes[parent].children<<n.id;}
        else {auto &kids=nodes[parent].children;int pos=kids.indexOf(selected);kids.insert(pos+1,n.id);}
        selected=n.id;render();editSelected();
    }
    QString findParent(const QString &id)const{for(auto it=nodes.constBegin();it!=nodes.constEnd();++it)if(it.value().children.contains(id))return it.key();return QString();}
    void editSelected(){if(!nodes.contains(selected))return;bool ok=false;QString value=QInputDialog::getMultiLineText(this,QStringLiteral("编辑主题"),QStringLiteral("主题内容"),nodes[selected].text,&ok);
        if(ok && value!=nodes[selected].text){checkpoint();nodes[selected].text=value;render();}}
    void removeSelected(){if(selected==root || !nodes.contains(selected))return;
        checkpoint();QString p=findParent(selected);nodes[p].children.removeAll(selected);
        std::function<void(QString)> del=[&](QString id){QStringList cs=nodes[id].children;for(auto &c:cs)del(c);nodes.remove(id);};del(selected);selected=p;render();}
    void toggleCollapse(){if(!nodes.contains(selected) || nodes[selected].children.isEmpty())return;checkpoint();nodes[selected].collapsed=!nodes[selected].collapsed;render();}
    void chooseLayout(bool classic){if(mindMap==classic)return;mindMap=classic;render();updateTitle();}
    void saveAs(){QString p=QFileDialog::getSaveFileName(this,QStringLiteral("保存导图"),filePath.isEmpty()?"Untitled.smap.json":filePath,QStringLiteral("SimpleMap JSON (*.smap.json)"));if(p.isEmpty())return;
        if(!p.endsWith(".smap.json",Qt::CaseInsensitive))p+=".smap.json";writeFile(p);}
    void writeFile(QString p){QSaveFile file(p);if(!file.open(QIODevice::WriteOnly)){QMessageBox::warning(this,QStringLiteral("保存失败"),file.errorString());return;}
        QByteArray data=QJsonDocument(document()).toJson(QJsonDocument::Indented);
        if(file.write(data)!=data.size()||!file.commit()){QMessageBox::warning(this,QStringLiteral("保存失败"),file.errorString());return;}
        filePath=p;dirty=false;updateTitle();statusBar()->showMessage(QStringLiteral("已保存：")+p,4000);
    }
    void openFile(){if(!confirmDiscard())return;QString p=QFileDialog::getOpenFileName(this,QStringLiteral("打开导图"),QString(),QStringLiteral("SimpleMap JSON (*.smap.json *.json)"));if(p.isEmpty())return;
        QFile f(p);if(!f.open(QIODevice::ReadOnly)){QMessageBox::warning(this,QStringLiteral("打开失败"),f.errorString());return;}
        if(f.size()>50*1024*1024){QMessageBox::warning(this,QStringLiteral("打开失败"),QStringLiteral("文件过大"));return;}
        QByteArray b=f.readAll();QString error;if(!apply(b,&error)){QMessageBox::warning(this,QStringLiteral("打开失败"),error.isEmpty()?QStringLiteral("文件结构无效"):error);return;}
        filePath=p;dirty=false;undoStack.clear();redoStack.clear();updateTitle();
    }
    void exportImage(bool svg){QString path=QFileDialog::getSaveFileName(this,QStringLiteral("导出图像"),QString(),svg?QStringLiteral("SVG (*.svg)"):QStringLiteral("PNG (*.png)"));if(path.isEmpty())return;
        if(svg){if(!path.endsWith(".svg",Qt::CaseInsensitive))path+=".svg";QSvgGenerator g;g.setFileName(path);QRectF bounds=scene->itemsBoundingRect().adjusted(-24,-24,24,24);g.setSize(bounds.size().toSize());g.setViewBox(QRect(QPoint(0,0),bounds.size().toSize()));QPainter pt(&g);scene->render(&pt,QRectF(QPointF(0,0),bounds.size()),bounds);}
        else {if(!path.endsWith(".png",Qt::CaseInsensitive))path+=".png";QRectF b=scene->itemsBoundingRect().adjusted(-24,-24,24,24);QImage im(b.size().toSize(),QImage::Format_ARGB32);im.fill(Qt::white);QPainter pt(&im);pt.setRenderHint(QPainter::Antialiasing);scene->render(&pt,QRectF(QPointF(0,0),b.size()),b);pt.end();if(!im.save(path))QMessageBox::warning(this,QStringLiteral("导出失败"),QStringLiteral("无法保存 PNG"));}
    }
    void makeMenus(){
        auto act=[&](QMenu *m,const QString &name,const QKeySequence &key,std::function<void()> f){QAction *a=m->addAction(name);if(!key.isEmpty())a->setShortcut(key);connect(a,&QAction::triggered,this,[f]{f();});return a;};
        QMenu *file=menuBar()->addMenu(QStringLiteral("文件"));
        act(file,QStringLiteral("新建"),QKeySequence("Ctrl+N"),[this]{if(confirmDiscard())resetDocument();});
        act(file,QStringLiteral("打开"),QKeySequence("Ctrl+O"),[this]{openFile();});
        act(file,QStringLiteral("保存"),QKeySequence("Ctrl+S"),[this]{filePath.isEmpty()?saveAs():writeFile(filePath);});
        act(file,QStringLiteral("另存为"),QKeySequence("Ctrl+Shift+S"),[this]{saveAs();});
        file->addSeparator();act(file,QStringLiteral("导出 PNG"),QKeySequence(),[this]{exportImage(false);});
        act(file,QStringLiteral("导出 SVG"),QKeySequence(),[this]{exportImage(true);});
        QMenu *edit=menuBar()->addMenu(QStringLiteral("编辑"));
        act(edit,QStringLiteral("新增同级"),QKeySequence("Return"),[this]{createNode(false);});
        act(edit,QStringLiteral("新增子级"),QKeySequence("Tab"),[this]{createNode(true);});
        act(edit,QStringLiteral("编辑主题"),QKeySequence("F2"),[this]{editSelected();});
        act(edit,QStringLiteral("删除节点"),QKeySequence("Delete"),[this]{removeSelected();});
        act(edit,QStringLiteral("折叠 / 展开"),QKeySequence("Space"),[this]{toggleCollapse();});
        edit->addSeparator();act(edit,QStringLiteral("撤销"),QKeySequence("Ctrl+Z"),[this]{history(true);});
        act(edit,QStringLiteral("重做"),QKeySequence("Ctrl+Y"),[this]{history(false);});
        QMenu *layout=menuBar()->addMenu(QStringLiteral("布局"));
        act(layout,QStringLiteral("经典思维导图"),QKeySequence("Ctrl+1"),[this]{chooseLayout(true);});
        act(layout,QStringLiteral("专业结构图"),QKeySequence("Ctrl+2"),[this]{chooseLayout(false);});
        act(layout,QStringLiteral("适应窗口"),QKeySequence("Ctrl+0"),[this]{canvas->fitInView(scene->itemsBoundingRect().adjusted(-60,-60,60,60),Qt::KeepAspectRatio);});
        QToolBar *bar=addToolBar(QStringLiteral("常用"));bar->setMovable(false);
        for(auto name:{QStringLiteral("新增子级"),QStringLiteral("编辑主题"),QStringLiteral("撤销"),QStringLiteral("重做")})for(QAction *a:edit->actions())if(a->text()==name)bar->addAction(a);
        bar->addSeparator();for(QAction *a:layout->actions())if(a->text()==QStringLiteral("经典思维导图")||a->text()==QStringLiteral("专业结构图"))bar->addAction(a);
    }
    void render(){
        rendering=true;scene->clear();positions.clear();graphics.clear();
        // Calculate subtree heights: descendants determine vertical space, not arbitrary coordinates.
        QMap<QString,double> heights;
        std::function<double(QString)> measure=[&](QString id)->double{
            Node n=nodes.value(id);if(n.collapsed||n.children.isEmpty())return heights[id]=56.;
            double h=0;for(QString c:n.children)h+=measure(c);return heights[id]=std::max(56.,h);
        };
        measure(root);
        std::function<void(QString,double,double,int)> placeTree=[&](QString id,double x,double center,int direction){
            positions[id]=QPointF(x,center);
            Node n=nodes.value(id);if(n.collapsed)return;
            double y=center-heights[id]/2.0;
            for(const QString &c:n.children){double h=heights[c];placeTree(c,x+direction*200,y+h/2.0,direction);y+=h;}
        };
        if(!mindMap){
            // Horizontal center by subtree width; depth on vertical axis.
            std::function<double(QString)> width=[&](QString id)->double{
                Node n=nodes.value(id);if(n.collapsed||n.children.isEmpty())return heights[id]=160;
                double w=0;for(QString c:n.children)w+=width(c);return heights[id]=std::max(160.,w);
            };width(root);
            std::function<void(QString,double,double)> place=[&](QString id,double x,double top){
                positions[id]=QPointF(x,top+heights[id]/2);Node n=nodes.value(id);if(n.collapsed)return;
                double off=top;for(QString c:n.children){place(c,x+145,off);off+=heights[c];}
            };place(root,0,0);
            // Rotate the logical orientation so parent-to-child is top-to-bottom.
            for(auto it=positions.begin();it!=positions.end();++it){QPointF p=it.value();it.value()=QPointF(p.y(),p.x());}
        }else{
            positions[root]=QPointF(0,0);QStringList left,right;
            for(int i=0;i<nodes[root].children.size();++i){QString c=nodes[root].children[i];(i%2==0?right:left).append(c);}
            for(int side=0;side<2;++side){QStringList cs=side==0?right:left;int dir=side==0?1:-1;double total=0;for(QString c:cs)total+=heights[c];double y=-total/2;
                for(QString c:cs){placeTree(c,200*dir,y+heights[c]/2,dir);y+=heights[c];}
            }
        }
        for(auto it=positions.constBegin();it!=positions.constEnd();++it){QString id=it.key();if(id==root)continue;
            QString p=findParent(id);if(!positions.contains(p))continue;QPointF a=positions[p],b=it.value();QPainterPath path;
            if(mindMap){double startX=a.x()+(b.x()>a.x()?64:-64);double endX=b.x()+(b.x()>a.x()?-64:64);
                path.moveTo(startX,a.y());path.cubicTo((startX+endX)/2,a.y(),(startX+endX)/2,b.y(),endX,b.y());}
            else{path.moveTo(a.x(),a.y()+19);path.cubicTo(a.x(),(a.y()+b.y())/2,b.x(),(a.y()+b.y())/2,b.x(),b.y()-19);}
            QPen pen(QColor("#93b2cd"),2);pen.setCosmetic(true);scene->addPath(path,pen);
        }
        for(auto it=positions.constBegin();it!=positions.constEnd();++it){QString id=it.key();Node n=nodes[id];QPointF p=it.value();
            QGraphicsRectItem *rect=scene->addRect(QRectF(-64,-19,128,38),QPen(id==selected?QColor("#e29b41"):QColor("#a9c4df"),id==selected?2.5:1),QBrush(id==root?QColor("#245d99"):QColor("#e6f0fa")));
            rect->setPos(p);rect->setData(0,id);rect->setFlag(QGraphicsItem::ItemIsSelectable);
            QGraphicsTextItem *label=scene->addText(n.text);label->setDefaultTextColor(id==root?Qt::white:QColor("#234866"));label->setTextWidth(120);
            QFont ft=label->font();ft.setPointSize(10);label->setFont(ft);
            QRectF tb=label->boundingRect();label->setPos(p-QPointF(60,std::min(tb.height()/2.0,15.0)));label->setAcceptedMouseButtons(Qt::NoButton);
            graphics[id]=rect;
            if(!n.children.isEmpty() && n.collapsed){auto badge=scene->addEllipse(p.x()+57,p.y()-24,13,13,QPen(Qt::white),QBrush(QColor("#de984b")));badge->setAcceptedMouseButtons(Qt::NoButton);}
        }
        scene->setSceneRect(scene->itemsBoundingRect().adjusted(-80,-80,80,80));
        if(canvas && positions.size()<=5)canvas->fitInView(scene->sceneRect(),Qt::KeepAspectRatio);
        rendering=false;updateTitle();
    }
protected:
    bool eventFilter(QObject *obj,QEvent *event) override {return QMainWindow::eventFilter(obj,event);}
    void mousePressEvent(QMouseEvent *e) override {QMainWindow::mousePressEvent(e);}
    bool event(QEvent *e) override {return QMainWindow::event(e);}
    void closeEvent(QCloseEvent *e) override {confirmDiscard()?e->accept():e->ignore();}
public:
    // Click handling is delegated to the scene's selectionChanged signal.
    void activateSelection(){QList<QGraphicsItem*> items=scene->selectedItems();if(items.isEmpty())return;
        QString id=items.first()->data(0).toString();if(!id.isEmpty() && nodes.contains(id)){selected=id;render();}}
    void activateDoubleClick(QGraphicsSceneMouseEvent *e){Q_UNUSED(e);editSelected();}
    void bindSelection(){connect(scene,&QGraphicsScene::selectionChanged,this,[this]{if(!rendering)activateSelection();});}
};
int main(int argc,char *argv[]){QApplication app(argc,argv);app.setApplicationName("SimpleMap");
    Window w;w.bindSelection();w.show();return app.exec();}
