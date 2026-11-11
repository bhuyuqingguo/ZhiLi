    static QString newElementId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

    QString decorationAt(QPoint at) const {
        const QPointF point = canvas->mapToScene(at);
        QPainterPathStroker stroke;
        stroke.setWidth(14 / qMax(0.05, canvas->transform().m11()));
        for (auto *item : scene->items()) {
            if (item->data(1).toString().isEmpty()) continue;
            if (auto *path = dynamic_cast<QGraphicsPathItem *>(item)) {
                if (stroke.createStroke(path->path()).contains(path->mapFromScene(point))) return item->data(1).toString();
            } else if (item->contains(item->mapFromScene(point))) return item->data(1).toString();
        }
        return {};
    }

    void beginRelationship(bool straight) {
        placingFloating = false; relationshipStraight = straight;
        const auto chosen = selectedRoots(true);
        if (chosen.size() == 2) { addRelationship(chosen[0], chosen[1], straight); return; }
        relationshipSource = selected;
        canvas->setCursor(Qt::CrossCursor); updateActionStates();
        statusBar()->showMessage(QStringLiteral("点击终点主题创建关系线 · Esc 取消 · 双击关系线编辑文字"));
    }

    void addRelationship(const QString &from, const QString &to, bool straight) {
        if (from == to || !doc.nodes.contains(from) || !doc.nodes.contains(to)) return;
        if (doc.relationships.size() >= Document::maxNodes) return;
        checkpoint();
        Relationship link; link.id = newElementId(); link.from = from; link.to = to; link.straight = straight;
        if (items.contains(from) && items.contains(to)) {
            const QPointF delta = items.value(to).body->pos() - items.value(from).body->pos();
            const qreal length = QLineF(QPointF(), delta).length();
            if (length > 0) link.bend = QPointF(delta.y(), -delta.x()) * (110 / length);
        }
        doc.relationships.append(link); selectedDecoration = link.id; changed();
    }

    void addBoundary() {
        const auto members = selectedRoots(true);
        if (members.isEmpty() || doc.boundaries.size() >= Document::maxNodes) return;
        for (const auto &frame : doc.boundaries) if (frame.members == members) {
            selectedDecoration = frame.id; render(); return;
        }
        checkpoint(); Boundary frame; frame.id = newElementId(); frame.members = members;
        doc.boundaries.append(frame); selectedDecoration = frame.id; changed();
    }

    void editDecoration() {
        QString text;
        bool found = false;
        for (const auto &link : doc.relationships) if (link.id == selectedDecoration) { text = link.text; found = true; }
        for (const auto &frame : doc.boundaries) if (frame.id == selectedDecoration) { text = frame.text; found = true; }
        if (!found) return;
        bool ok = false;
        const QString next = QInputDialog::getMultiLineText(this, QStringLiteral("编辑标题"), QStringLiteral("说明文字（可以留空）"), text, &ok);
        if (!ok || text == next || next.size() > Document::maxText) return;
        checkpoint();
        for (auto &link : doc.relationships) if (link.id == selectedDecoration) link.text = next;
        for (auto &frame : doc.boundaries) if (frame.id == selectedDecoration) frame.text = next;
        changed();
    }

    void deleteDecoration() {
        bool exists = false;
        for (const auto &link : doc.relationships) exists |= link.id == selectedDecoration;
        for (const auto &frame : doc.boundaries) exists |= frame.id == selectedDecoration;
        if (!exists) { selectedDecoration.clear(); return; }
        checkpoint();
        for (int i = doc.relationships.size() - 1; i >= 0; --i) if (doc.relationships[i].id == selectedDecoration) doc.relationships.removeAt(i);
        for (int i = doc.boundaries.size() - 1; i >= 0; --i) if (doc.boundaries[i].id == selectedDecoration) doc.boundaries.removeAt(i);
        selectedDecoration.clear(); changed();
    }

    void copyDecoration(bool cut) {
        const auto json = QJsonDocument::fromJson(doc.serialize()).object();
        QJsonObject payload;
        for (const auto &key : {QStringLiteral("relationships"), QStringLiteral("boundaries")})
            for (const auto &entry : json.value(key).toArray()) if (entry.toObject().value("id").toString() == selectedDecoration)
                payload.insert(key, QJsonArray{entry});
        if (payload.isEmpty()) return;
        const auto bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        if (writeClipboard([&] {
            auto *mime = new QMimeData; mime->setData("application/x-zhili-decoration+json", bytes); return mime;
        }) && cut) deleteDecoration();
    }

    void pasteDecoration(const QByteArray &bytes) {
        if (bytes.size() > Document::maxBytes) return;
        const auto payload = QJsonDocument::fromJson(bytes).object();
        auto next = QJsonDocument::fromJson(doc.serialize()).object();
        QString inserted;
        for (const auto &key : {QStringLiteral("relationships"), QStringLiteral("boundaries")}) {
            const auto incoming = payload.value(key).toArray();
            if (incoming.size() > 1) return;
            auto entries = next.value(key).toArray();
            for (const auto &entry : incoming) {
                auto fields = entry.toObject(); inserted = newElementId(); fields.insert("id", inserted); entries.append(fields);
            }
            if (!entries.isEmpty()) next.insert(key, entries);
        }
        if (inserted.isEmpty()) return;
        next.insert("version", 2);
        Document checked; QString error;
        if (!checked.parse(QJsonDocument(next).toJson(), &error)) {
            statusBar()->showMessage(QStringLiteral("粘贴对象需要原主题仍在当前导图中；跨文件请连同分支复制"), 5000); return;
        }
        checkpoint(); doc = checked; selectedDecoration = inserted; changed();
    }

    void createFloating(QPointF position) {
        if (!focusRoot.isEmpty()) { leaveFocus(); }
        if (doc.nodes.size() >= Document::maxNodes) return;
        checkpoint(); Node node; node.id = newElementId(); node.text = QStringLiteral("浮动主题");
        doc.nodes.insert(node.id, node); doc.nodes[doc.root].children.append(node.id);
        doc.floating.insert(node.id, position); selected = node.id; additionalSelection.clear(); selectedDecoration.clear();
        changed(); startEdit();
    }

    void floatBranches(QStringList roots, QPointF offset, bool copy) {
        roots.removeAll(doc.root);
        if (roots.isEmpty() || !focusRoot.isEmpty()) return;
        Document next = doc;
        QMap<QString, QPointF> positions;
        for (const auto &id : roots) positions.insert(id, items.value(id).body->pos() + offset);
        if (copy) {
            QString error; QStringList inserted;
            if (!BranchClipboard::paste(&next, doc.root, BranchClipboard::encode(doc, roots, &error), &inserted, &error)) return;
            for (int i = 0; i < roots.size(); ++i) next.floating.insert(inserted[i], positions.value(roots[i]));
            roots = inserted;
        } else {
            for (const auto &id : roots) {
                next.nodes[parents.value(id)].children.removeAll(id);
                next.nodes[next.root].children.append(id);
                QPointF point = positions.value(id);
                // Snap each axis independently to nearby topic centers.
                for (auto it = items.cbegin(); it != items.cend(); ++it) {
                    QString ancestor = it.key();
                    while (!ancestor.isEmpty() && ancestor != id) ancestor = parents.value(ancestor);
                    if (roots.contains(it.key()) || ancestor == id) continue;
                    const QPointF other = it->body->pos();
                    if (qAbs(point.x() - other.x()) < 8) point.setX(other.x());
                    if (qAbs(point.y() - other.y()) < 8) point.setY(other.y());
                }
                point.setX(qBound(-10000000.0, point.x(), 10000000.0));
                point.setY(qBound(-10000000.0, point.y(), 10000000.0));
                next.floating.insert(id, point);
            }
        }
        if (next.serialize() == doc.serialize()) return;
        checkpoint(); doc = next; changed(); replaceSelection(roots);
    }

    void previewDecoration(const QPointF &delta) {
        const Document original = doc;
        for (auto &link : doc.relationships) if (link.id == decorationDrag) { link.straight = false; link.bend += delta; }
        renderDecorations(scene, items);
        doc = original;
    }

    void moveDecoration(const QPointF &delta) {
        if (delta.isNull()) return;
        for (int i = 0; i < doc.relationships.size(); ++i) if (doc.relationships[i].id == selectedDecoration) {
            checkpoint(); auto &link = doc.relationships[i]; link.straight = false; link.bend += delta;
            link.bend.setX(qBound(-10000000.0, link.bend.x(), 10000000.0));
            link.bend.setY(qBound(-10000000.0, link.bend.y(), 10000000.0));
            changed(); return;
        }
        // A boundary follows its topics; moving it never tears a tree apart.
        statusBar()->showMessage(QStringLiteral("外框随分支自动调整；拖动主题可移动分支"), 4000);
    }

    void renderDecorations(QGraphicsScene *target, const QMap<QString, NodeItem> &topics) {
        for (auto *item : target->items()) if (!item->data(1).toString().isEmpty()) delete item;
        const auto tag = [](QGraphicsItem *item, const QString &id) { item->setData(1, id); item->setAcceptedMouseButtons(Qt::NoButton); };
        const auto label = [&](const QString &text, const QString &id, QPointF at) {
            if (text.isEmpty()) return;
            auto *item = target->addText(text, QApplication::font());
            item->setDefaultTextColor(ink); item->setTextWidth(qMin(240.0, item->boundingRect().width()));
            alignTopicText(item->document(), TextAlignment::Center);
            item->setPos(at - QPointF(item->boundingRect().width() / 2, item->boundingRect().height() / 2));
            item->setZValue(5); tag(item, id);
            auto *back = target->addRect(item->sceneBoundingRect().adjusted(-4, -2, 4, 2), Qt::NoPen, paper);
            back->setZValue(4); tag(back, id);
        };
        for (const auto &frame : doc.boundaries) {
            QRectF bounds; QStringList pending = frame.members; QSet<QString> visited;
            while (!pending.isEmpty()) {
                const QString id = pending.takeLast();
                if (visited.contains(id)) continue;
                visited.insert(id);
                if (topics.contains(id)) bounds = bounds.united(topics.value(id).body->sceneBoundingRect());
                pending.append(doc.nodes.value(id).children);
            }
            if (bounds.isEmpty()) continue;
            bounds.adjust(-22, -22, 22, 22);
            QPainterPath path; path.addRoundedRect(bounds, 22, 22);
            const bool selected = target == scene && selectedDecoration == frame.id;
            auto *item = target->addPath(path, QPen(selected ? accent : QColor("#71a48b"), selected ? 2.6 : 1.5, Qt::DashLine), QColor(79, 146, 109, 16));
            item->setZValue(-3); tag(item, frame.id);
            label(frame.text, frame.id, QPointF(bounds.center().x(), bounds.top()));
        }
        const auto anchor = [](const QRectF &rect, QPointF toward) {
            QPointF delta = toward - rect.center();
            const qreal ratio = qMax(qAbs(delta.x()) / qMax(1.0, rect.width()/2), qAbs(delta.y()) / qMax(1.0, rect.height()/2));
            return ratio > 0 ? rect.center() + delta / ratio : rect.center();
        };
        for (const auto &link : doc.relationships) {
            if (!topics.contains(link.from) || !topics.contains(link.to)) continue;
            const QRectF from = topics.value(link.from).body->sceneBoundingRect(), to = topics.value(link.to).body->sceneBoundingRect();
            const QPointF middle = (from.center() + to.center()) / 2;
            const QPointF control = middle + link.bend;
            const QPointF a = anchor(from, link.straight ? to.center() : control);
            const QPointF b = anchor(to, link.straight ? from.center() : control);
            QPainterPath path(a);
            if (link.straight) path.lineTo(b); else path.quadTo(control, b);
            const bool selected = target == scene && selectedDecoration == link.id;
            const QColor color = selected ? accent : QColor("#90704b");
            auto *item = target->addPath(path, QPen(color, selected ? 2.8 : 1.8, Qt::DashLine, Qt::RoundCap));
            item->setZValue(0); tag(item, link.id);
            QLineF direction(link.straight ? a : control, b);
            if (direction.length() > 0) {
                direction.setLength(10); const QPointF v = direction.p2() - direction.p1(), n(-v.y() * .45, v.x() * .45);
                QPainterPath arrow(b); arrow.lineTo(b - v + n); arrow.lineTo(b - v - n); arrow.closeSubpath();
                auto *tip = target->addPath(arrow, Qt::NoPen, color); tag(tip, link.id);
            }
            QPointF labelAt = path.pointAtPercent(.5);
            QTextDocument labelMeasure;
            labelMeasure.setDefaultFont(QApplication::font()); labelMeasure.setPlainText(link.text);
            labelMeasure.setTextWidth(qMin(240.0, labelMeasure.idealWidth()));
            const QSizeF labelSize = labelMeasure.size() + QSizeF(8, 4);
            QPointF labelStep = link.bend;
            const qreal stepLength = QLineF(QPointF(), labelStep).length();
            labelStep = stepLength > 0 ? labelStep * (12 / stepLength) : QPointF(0, -12);
            for (int attempt = 0; attempt < 30 && !link.text.isEmpty(); ++attempt) {
                const QRectF box(labelAt - QPointF(labelSize.width()/2, labelSize.height()/2), labelSize);
                bool overlaps = false;
                for (const auto &topic : topics) overlaps |= box.intersects(topic.body->sceneBoundingRect());
                if (!overlaps) break;
                labelAt += labelStep;
            }
            label(link.text, link.id, labelAt);
            if (selected) {
                auto *handle = target->addEllipse(QRectF(-5,-5,10,10), QPen(QColor("#b68d3c")), QColor("#f9db83"));
                handle->setPos(control); handle->setZValue(7); tag(handle, link.id);
                handle->setData(2, true); handle->setData(3, control);
                handle->setToolTip(QStringLiteral("拖动调整曲率 · 双击编辑标题 · Delete 删除"));
            }
        }
    }
