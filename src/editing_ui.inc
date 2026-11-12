    bool formatBrush = false;
    QTimer versionTimer;
    FindOptions lastFindOptions;

    void styleEditingDialog(QDialog &dialog) {
        if(appearance.highContrast) return;
        dialog.setStyleSheet(QStringLiteral("QDialog{background:#f3f6f0;color:#244d3d} QLabel,QCheckBox{color:#244d3d} QLineEdit,QPlainTextEdit,QListWidget,QComboBox{background:#ffffff;color:#244d3d;border:1px solid #c8d8c8;border-radius:6px;padding:7px;selection-background-color:#dcebdd;selection-color:#193f31} QPushButton{background:#ffffff;color:#285b48;border:1px solid #c8d8c8;border-radius:6px;padding:8px 13px} QPushButton:hover{background:#e4efdf} QPushButton:default{background:#285b48;color:#ffffff}"));
    }

    void pasteSpecial() {
        QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("选择性粘贴"));
        styleEditingDialog(dialog);
        QFormLayout layout(&dialog);
        QComboBox mode; mode.addItems({QStringLiteral("原始分支 / 自动识别 Markdown"), QStringLiteral("纯文本大纲（按缩进）"), QStringLiteral("Markdown 标题与列表"), QStringLiteral("文字合为一个主题"), QStringLiteral("仅粘贴已复制的主题样式")});
        QComboBox placement; placement.addItems({QStringLiteral("作为子主题"), QStringLiteral("作为同级主题")});
        QCheckBox style(QStringLiteral("保留文字样式")), notes(QStringLiteral("保留备注")); style.setChecked(true); notes.setChecked(true);
        layout.addRow(QStringLiteral("内容"), &mode); layout.addRow(QStringLiteral("位置"), &placement);
        layout.addRow(&style); layout.addRow(&notes);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout.addRow(&buttons);
        buttons.button(QDialogButtonBox::Ok)->setText(QStringLiteral("粘贴")); buttons.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        if (mode.currentIndex() == 4) { if(hasCopiedStyle) pasteTopicStyle(); else statusBar()->showMessage(QStringLiteral("请先选中来源主题并启动格式刷以复制样式"),5000); return; }
        pasteBranch(placement.currentIndex() == 1, static_cast<PasteContent>(mode.currentIndex()), style.isChecked(), notes.isChecked());
    }

    void findReplace() {
        QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("查找与替换")); dialog.resize(520, 450);
        styleEditingDialog(dialog);
        QVBoxLayout layout(&dialog); QFormLayout form;
        QLineEdit find(query), replacement; find.setObjectName("findText"); replacement.setObjectName("replacementText");
        form.addRow(QStringLiteral("查找"), &find); form.addRow(QStringLiteral("替换为"), &replacement); layout.addLayout(&form);
        QCheckBox text(QStringLiteral("主题文字")), notes(QStringLiteral("备注")), sensitive(QStringLiteral("区分大小写")), hidden(QStringLiteral("包含折叠主题")), branch(QStringLiteral("仅当前分支"));
        text.setChecked(true); hidden.setChecked(true);
        QHBoxLayout options; for (auto *box : {&text, &notes, &sensitive}) options.addWidget(box); layout.addLayout(&options);
        QHBoxLayout scope; scope.addWidget(&hidden); scope.addWidget(&branch); layout.addLayout(&scope);
        QListWidget results; results.setObjectName("findResults"); layout.addWidget(&results);
        QLabel status; layout.addWidget(&status);
        const QString scopeRoot = selected;
        const auto settings = [&] { FindOptions o; o.text=text.isChecked(); o.notes=notes.isChecked(); o.caseSensitive=sensitive.isChecked(); o.hidden=hidden.isChecked(); o.root=branch.isChecked()?scopeRoot:QString(); return o; };
        const auto refresh = [&] {
            const QString previous=results.currentItem()?results.currentItem()->data(Qt::UserRole).toString():QString();
            query = find.text(); lastFindOptions=settings(); results.clear(); int selectedRow=0;
            for (const auto &id : findTopics(doc, query, settings())) {
                auto *item = new QListWidgetItem(doc.nodes.value(id).text.section('\n',0,0).left(120), &results);
                item->setData(Qt::UserRole, id); item->setToolTip(doc.nodes.value(id).notes);
                if(id==previous) selectedRow=results.count()-1;
            }
            status.setText(QStringLiteral("%1 个匹配主题").arg(results.count()));
            if (results.count()) results.setCurrentRow(selectedRow);
        };
        connect(&find, &QLineEdit::textChanged, &dialog, refresh);
        for (auto *box : {&text, &notes, &sensitive, &hidden, &branch}) connect(box, &QCheckBox::toggled, &dialog, refresh);
        connect(&results, &QListWidget::currentItemChanged, &dialog, [&](QListWidgetItem *item) { if (item) { lastMatch=item->data(Qt::UserRole).toString(); revealTopic(lastMatch); } });
        QHBoxLayout actions; QPushButton next(QStringLiteral("下一个")), one(QStringLiteral("替换一次")), all(QStringLiteral("全部替换")), close(QStringLiteral("关闭"));
        for (auto *button : {&next,&one,&all,&close}) { button->setAutoDefault(false); actions.addWidget(button); } layout.addLayout(&actions);
        connect(&next,&QPushButton::clicked,&dialog,[&] { if(results.count()) results.setCurrentRow((results.currentRow()+1)%results.count()); });
        const auto replace = [&](bool first) {
            QStringList ids; if (first && results.currentItem()) ids.append(results.currentItem()->data(Qt::UserRole).toString());
            if (!first) ids = findTopics(doc,find.text(),settings());
            Document changedDoc=doc; QString error; int count=0;
            if (!replaceTopics(&changedDoc,ids,find.text(),replacement.text(),settings(),first,&count,&error)) {
                status.setText(error.isEmpty()?QStringLiteral("请输入查找文字"):error); return;
            }
            if (count) { checkpoint(); doc=std::move(changedDoc); changed(); }
            refresh(); status.setText(QStringLiteral("已替换 %1 处，可用 Ctrl+Z 撤销").arg(count));
        };
        connect(&one,&QPushButton::clicked,&dialog,[&] { replace(true); });
        connect(&all,&QPushButton::clicked,&dialog,[&] { replace(false); });
        connect(&close,&QPushButton::clicked,&dialog,&QDialog::accept);
        refresh(); find.setFocus(); dialog.exec();
        for(auto *child:dialog.findChildren<QObject *>()) QObject::disconnect(child,nullptr,&dialog,nullptr);
    }

    bool setTopicImage(const QImage &original) {
        if (original.isNull() || !doc.nodes.contains(selected)) return false;
        const QImage image = original.width()>1600 || original.height()>1600 ? original.scaled(1600,1600,Qt::KeepAspectRatio,Qt::SmoothTransformation) : original;
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer,"PNG") || png.size() > 8*1024*1024) return false;
        Document next=doc; next.nodes[selected].imagePng=png; Document checked; QString error;
        if (!checked.parse(next.serialize(),&error)) { statusBar()->showMessage(error,6000); return false; }
        checkpoint(); doc=std::move(checked); changed(); return true;
    }

    void chooseImage() {
        const QString path=QFileDialog::getOpenFileName(this,QStringLiteral("插入 / 替换图片"),{},QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (path.isEmpty()) return;
        QImageReader reader(path); const QSize size=reader.size();
        if (!size.isValid() || qint64(size.width())*size.height()>40000000 || QFileInfo(path).size()>Document::maxBytes) {
            QMessageBox::warning(this,QStringLiteral("无法读取图片"),QStringLiteral("请选择小于 40 百万像素、50 MiB 的图片")); return;
        }
        reader.setAutoTransform(true); reader.setScaledSize(size.scaled(1600,1600,Qt::KeepAspectRatio));
        if (!setTopicImage(reader.read())) QMessageBox::warning(this,QStringLiteral("无法读取图片"),reader.errorString());
    }

    void editLinks() {
        QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("主题链接")); dialog.resize(540,300);
        styleEditingDialog(dialog);
        QVBoxLayout layout(&dialog); QLabel hint(QStringLiteral("每行一个网址、mailto: 邮箱或 file: 文件链接，最多 32 个。")); layout.addWidget(&hint);
        QPlainTextEdit input(doc.nodes.value(selected).links.join('\n')); layout.addWidget(&input);
        QPushButton file(QStringLiteral("添加本地文件…")); layout.addWidget(&file);
        connect(&file,&QPushButton::clicked,&dialog,[&] { const auto path=QFileDialog::getOpenFileName(&dialog,QStringLiteral("选择链接文件")); if(!path.isEmpty()) input.appendPlainText(QUrl::fromLocalFile(path).toString()); });
        QLabel error; layout.addWidget(&error);
        QDialogButtonBox buttons(QDialogButtonBox::Save|QDialogButtonBox::Cancel); layout.addWidget(&buttons);
        buttons.button(QDialogButtonBox::Save)->setText(QStringLiteral("保存")); buttons.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&] {
            QStringList links; for (const auto &line : input.toPlainText().split('\n')) {
                QString link=line.trimmed(); if(link.isEmpty()) continue;
                if (QDir::isAbsolutePath(link)) link=QUrl::fromLocalFile(link).toString();
                else if (!QUrl(link).isRelative()) {} else link="https://"+link;
                links.append(link);
            }
            Document next=doc; next.nodes[selected].links=links; Document checked; QString message;
            if (!checked.parse(next.serialize(),&message)) { error.setText(QStringLiteral("链接格式无效：仅支持 http、https、mailto、file")); return; }
            if(next.serialize()!=doc.serialize()) { checkpoint(); doc=std::move(checked); changed(); } dialog.accept();
        }); dialog.exec();
        for(auto *child:dialog.findChildren<QObject *>()) QObject::disconnect(child,nullptr,&dialog,nullptr);
    }

    void openTopicLinks() {
        const auto links=doc.nodes.value(selected).links; if(links.isEmpty()) return;
        QMenu menu(this);
        for (const auto &link : links) {
            QAction *entry=menu.addAction(link.left(120)); entry->setToolTip(link);
            connect(entry,&QAction::triggered,this,[this,link] {
                const QUrl url(link); if(url.isLocalFile() && QMessageBox::question(this,QStringLiteral("打开本地链接"),QStringLiteral("使用系统默认程序打开：\n%1").arg(url.toLocalFile()))!=QMessageBox::Yes) return;
                if(!QDesktopServices::openUrl(url)) statusBar()->showMessage(QStringLiteral("系统无法打开此链接"),5000);
            });
        }
        menu.exec(QCursor::pos());
    }

    void showVersions() {
        if(filePath.isEmpty()) { QMessageBox::information(this,QStringLiteral("本地历史版本"),QStringLiteral("请先保存导图，之后每次保存和编辑中的每分钟都会保留历史快照。")); return; }
        QString error; if(!LocalHistory::record(filePath,doc.serialize(),&error)) { QMessageBox::warning(this,QStringLiteral("历史快照失败"),error); return; }
        QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("本地历史版本 · 最近 30 份 / 200 MiB")); dialog.resize(650,450);
        styleEditingDialog(dialog);
        QVBoxLayout layout(&dialog); QListWidget list; QPlainTextEdit preview; preview.setReadOnly(true);
        layout.addWidget(&list); layout.addWidget(&preview);
        for(const auto &path:LocalHistory::versions(filePath)) {
            auto *item=new QListWidgetItem(QFileInfo(path).lastModified().toLocalTime().toString("yyyy-MM-dd HH:mm:ss.zzz")+QStringLiteral("  ·  %1 KiB").arg(QFileInfo(path).size()/1024),&list); item->setData(Qt::UserRole,path);
        }
        connect(&list,&QListWidget::currentItemChanged,&dialog,[&](QListWidgetItem *item) {
            Document version; QString message;
            if(item && version.readFile(item->data(Qt::UserRole).toString(),&message)) preview.setPlainText(BranchClipboard::outline(version,version.root).left(30000));
            else preview.setPlainText(message);
        });
        QLabel hint(QStringLiteral("恢复后成为当前未保存内容；可撤销，点击保存才覆盖原文件。历史仅保存在本机。")); hint.setWordWrap(true); layout.addWidget(&hint);
        QDialogButtonBox buttons(QDialogButtonBox::RestoreDefaults|QDialogButtonBox::Close); buttons.button(QDialogButtonBox::RestoreDefaults)->setText(QStringLiteral("恢复选中版本")); layout.addWidget(&buttons);
        buttons.button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
        connect(buttons.button(QDialogButtonBox::Close),&QPushButton::clicked,&dialog,&QDialog::reject);
        connect(buttons.button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,&dialog,[&] {
            if(!list.currentItem()) return; Document version; QString message;
            if(!version.readFile(list.currentItem()->data(Qt::UserRole).toString(),&message)) { preview.setPlainText(message); return; }
            checkpoint(); doc=std::move(version); selected=doc.root; additionalSelection.clear(); focusRoot.clear(); changed(); fit(); dialog.accept();
        }); if(list.count()) list.setCurrentRow(0); dialog.exec();
        for(auto *child:dialog.findChildren<QObject *>()) QObject::disconnect(child,nullptr,&dialog,nullptr);
    }
