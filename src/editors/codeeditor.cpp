#include "editors/codeeditor.h"
#include "base/application.h"
#include "base/utils.h"
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Theme>
#include <QBoxLayout>
#include <QDebug>
#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextStream>

// CodeEditor:

CodeEditor::CodeEditor(const ResourceModelIndex &index, QWidget *parent) : FileEditor(index, parent)
{
    const QString filename = index.path();
    title = filename.section('/', -2);
    QRegularExpression guid("^{\\w{8}-\\w{4}-\\w{4}-\\w{4}-\\w{12}}");
    if (guid.match(title).hasMatch()) {
        title = title.split('/').last();
    }
    icon = index.icon();

    editor = new CodeTextEdit(this);
    editor->setDefinition(app->highlightingRepository.definitionForFileName(filename));
    new LineNumberArea(editor);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(editor);
    layout->setMargin(0);

    file = new QFile(filename, this);
    load();

    connect(editor, &QPlainTextEdit::modificationChanged, this, &CodeEditor::setModified);
}

bool CodeEditor::load()
{
    file->close();
    if (file->open(QFile::ReadWrite)) {
        QTextStream stream(file);
        stream.setCodec("UTF-8");
        auto cursor = editor->textCursor();
        const int selectionStart = cursor.selectionStart();
        const int selectionEnd = cursor.selectionEnd();
        const int scrollPosition = editor->verticalScrollBar()->value();
        editor->setPlainText(stream.readAll());
        codec = stream.codec();
        cursor.setPosition(selectionStart);
        cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        editor->verticalScrollBar()->setValue(scrollPosition);
        setModified(false);
        return true;
    } else {
        qWarning() << "Error: Could not open code resource file";
        return false;
    }
}

bool CodeEditor::save(const QString &as)
{
    QFile *file = this->file;
    if (!as.isEmpty()) {
        file = new QFile(as, this);
        file->open(QFile::WriteOnly);
    }

    bool result;
    if (file->isWritable()) {
        file->resize(0);
        QTextStream stream(file);
        stream.setCodec(codec);
        stream.setGenerateByteOrderMark(codec->name() != "UTF-8");
        stream << editor->toPlainText();
        if (as.isEmpty()) {
            setModified(false);
            emit saved();
        }
        result = true;
    } else {
        qWarning() << "Error: Could not save code resource file";
        result = false;
    }

    if (!as.isEmpty()) {
        delete file;
    }
    return result;
}

// CodeTextEdit:

CodeTextEdit::CodeTextEdit(QWidget *parent)
    : QPlainTextEdit(parent)
    , highlighter(new KSyntaxHighlighting::SyntaxHighlighter(document()))
{
    setLineWrapMode(CodeTextEdit::NoWrap);

    const auto defaultTheme = app->highlightingRepository.defaultTheme(Utils::isDarkTheme()
        ? KSyntaxHighlighting::Repository::DarkTheme
        : KSyntaxHighlighting::Repository::LightTheme);
    setTheme(defaultTheme);

#if defined(Q_OS_WIN)
    QFont font("Consolas");
    font.setPointSize(11);
#elif defined(Q_OS_OSX)
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(12);
#elif defined(Q_OS_LINUX)
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(10);
#endif
    setFont(font);
}

const KSyntaxHighlighting::Theme CodeTextEdit::getTheme() const
{
    return highlighter->theme();
}

void CodeTextEdit::setTheme(const KSyntaxHighlighting::Theme &theme)
{
    auto newPalette(palette());
    newPalette.setColor(QPalette::Base, theme.editorColor(KSyntaxHighlighting::Theme::BackgroundColor));
    newPalette.setColor(QPalette::Highlight, theme.editorColor(KSyntaxHighlighting::Theme::TextSelection));
    setPalette(newPalette);
    highlighter->setTheme(theme);
    highlighter->rehighlight();
}

void CodeTextEdit::setDefinition(const KSyntaxHighlighting::Definition &definition)
{
    highlighter->setDefinition(definition);
    highlighter->rehighlight();
    setTabStopDistance(4 * QFontMetrics(font()).horizontalAdvance(' '));
}

void CodeTextEdit::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event)
    emit resized();
}

// CodeContainer:

LineNumberArea::LineNumberArea(CodeTextEdit *parent) : QWidget(parent), editor(parent)
{
    lineNumberAreaWidth = 0;
    lineNumberAreaPadding = 20;
    connect(editor, &CodeTextEdit::blockCountChanged, this, &LineNumberArea::updateAreaWidth);
    connect(editor, &CodeTextEdit::resized, this, &LineNumberArea::updateAreaGeometry);
    connect(editor, &CodeTextEdit::cursorPositionChanged, this, [=]() {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(QColor(editor->getTheme().editorColor(KSyntaxHighlighting::Theme::CurrentLine)));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = editor->textCursor();
        selection.cursor.clearSelection();
        editor->setExtraSelections({selection});
        currentLineNumber = editor->textCursor().block().blockNumber() + 1;
    });
    connect(editor, &CodeTextEdit::updateRequest, this, [=](const QRect &rect, int dy) {
        dy ? scroll(0, dy) : update(0, rect.y(), width(), rect.height());
    });
}

QSize LineNumberArea::sizeHint() const
{
    return QSize(lineNumberAreaWidth, editor->viewport()->height());
}

void LineNumberArea::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(0, 0, lineNumberAreaWidth, height(), editor->getTheme().editorColor(KSyntaxHighlighting::Theme::IconBorder));
    painter.setPen(editor->getTheme().editorColor(KSyntaxHighlighting::Theme::LineNumbers));

    const int lineNumberHeight = editor->fontMetrics().height();
    QTextBlock block = editor->firstVisibleBlock();
    int top = static_cast<int>(editor->blockBoundingGeometry(block).translated(editor->contentOffset()).top());
    int bottom = top + static_cast<int>(editor->blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.save();
            const int blockNumber = block.blockNumber() + 1;
            if (blockNumber == currentLineNumber) {
                painter.setPen(editor->getTheme().editorColor(KSyntaxHighlighting::Theme::CurrentLineNumber));
                QFont font = painter.font();
                font.setBold(true);
                painter.setFont(font);
            }
            painter.drawText(lineNumberAreaPadding / 2, top, lineNumberAreaWidth - lineNumberAreaPadding,
                             lineNumberHeight, Qt::AlignRight, QString::number(blockNumber));
            painter.restore();
        }
        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(editor->blockBoundingRect(block).height());
    }
}

void LineNumberArea::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LayoutDirectionChange) {
        updateAreaWidth(editor->blockCount());
        updateAreaGeometry();
    }
    QWidget::changeEvent(event);
}

void LineNumberArea::updateAreaWidth(int blocks)
{
    lineNumberAreaWidth = editor->fontMetrics().boundingRect(QString::number(blocks)).width() + lineNumberAreaPadding;
    if (layoutDirection() == Qt::LeftToRight) {
        editor->setViewportMargins(lineNumberAreaWidth, 0, 0, 0);
    } else {
        editor->setViewportMargins(0, 0, lineNumberAreaWidth, 0);
    }
}

void LineNumberArea::updateAreaGeometry()
{
    const QRect contents = editor->contentsRect();
    if (layoutDirection() == Qt::LeftToRight) {
        setGeometry(QRect(contents.left(), contents.top(), lineNumberAreaWidth, contents.height()));
    } else {
        setGeometry(QRect(contents.right() - lineNumberAreaWidth, contents.top(), lineNumberAreaWidth, contents.height()));
    }
}
