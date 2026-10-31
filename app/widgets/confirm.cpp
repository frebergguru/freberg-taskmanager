#include "confirm.h"

#include <QAccessible>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace fg {

namespace {

// Not a QMessageBox, which can go back to guessing whether its text is markup. Here it is always plain.
class ConfirmDialog final : public QDialog {
    Q_OBJECT
public:
    ConfirmDialog(QWidget* parent, const QString& title, const QString& question) : QDialog(parent)
    {
        setObjectName(QStringLiteral("fgConfirm"));
        setWindowTitle(title);
        setAccessibleName(title);
        setAccessibleDescription(question);

        auto* icon = new QLabel(this);
        const int side = style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, this);
        icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning, nullptr, this).pixmap(side, side));
        auto* text = new QLabel(question, this);
        text->setTextFormat(Qt::PlainText);
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        text->setMinimumWidth(fontMetrics().averageCharWidth() * 40);

        auto* row = new QHBoxLayout;
        row->addWidget(icon, 0, Qt::AlignTop);
        row->addWidget(text, 1);

        auto* buttons = new QDialogButtonBox(this);
        QPushButton* yes = buttons->addButton(tr("&Yes"), QDialogButtonBox::AcceptRole);
        QPushButton* no = buttons->addButton(tr("&No"), QDialogButtonBox::RejectRole);
        yes->setAutoDefault(false);
        no->setDefault(true);
        no->setFocus();
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); // and Esc, which QDialog sends here

        auto* layout = new QVBoxLayout(this);
        layout->addLayout(row);
        layout->addWidget(buttons);
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QDialog::showEvent(event);
        QAccessibleEvent alert(this, QAccessible::Alert); // so a screen reader speaks the question
        QAccessible::updateAccessibility(&alert);
    }
};

} // namespace

bool confirmed(QWidget* parent, const QString& title, const QString& question)
{
    ConfirmDialog dialog(parent, title, question);
    return dialog.exec() == QDialog::Accepted;
}

} // namespace fg

#include "confirm.moc"
