#include "ui/leftrail.h"

#include <QButtonGroup>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace jit::dash::ui {

LeftRail::LeftRail(QWidget* parent) : QWidget(parent)
{
    setFixedWidth(56);
    setObjectName("leftRail");

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(4, 10, 4, 10);
    layout_->setSpacing(6);

    group_ = new QButtonGroup(this);
    group_->setExclusive(true);

    layout_->addStretch(); // nút section được chèn TRƯỚC stretch này (xem addSection)

    settingsBtn_ = new QPushButton;
    settingsBtn_->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    settingsBtn_->setObjectName("railButton");
    settingsBtn_->setFixedSize(44, 44);
    settingsBtn_->setToolTip("Settings");
    settingsBtn_->setCursor(Qt::PointingHandCursor);
    connect(settingsBtn_, &QPushButton::clicked, this, [this] { if (onSettingsClicked) onSettingsClicked(); });
    layout_->addWidget(settingsBtn_);

    accountBtn_ = new QPushButton;
    accountBtn_->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    accountBtn_->setObjectName("railAccountButton");
    accountBtn_->setFixedSize(44, 44);
    accountBtn_->setToolTip("Account");
    accountBtn_->setCursor(Qt::PointingHandCursor);
    connect(accountBtn_, &QPushButton::clicked, this, [this] { if (onAccountClicked) onAccountClicked(); });
    layout_->addWidget(accountBtn_);
}

int LeftRail::addSection(const QIcon& icon, const QString& tooltip)
{
    auto* btn = new QPushButton;
    btn->setIcon(icon);
    btn->setObjectName("railButton");
    btn->setFixedSize(44, 44);
    btn->setCheckable(true);
    btn->setToolTip(tooltip);
    btn->setCursor(Qt::PointingHandCursor);

    const int index = static_cast<int>(buttons_.size());
    group_->addButton(btn, index);
    buttons_.push_back(btn);

    // Chèn TRƯỚC stretch (luôn là item cuối cùng hiện có khi mới addSection
    // lần đầu, vì layout chỉ có đúng 1 stretch được thêm 1 lần trong
    // constructor). Dùng layout_ lưu sẵn - KHÔNG qobject_cast(this->layout())
    // (đã gây crash thật trong môi trường test, xem chú thích trong
    // leftrail.h - cùng dạng lỗi đã gặp ở CpuPage trước đó).
    layout_->insertWidget(static_cast<int>(buttons_.size()) - 1, btn);

    connect(btn, &QPushButton::clicked, this, [this, index] {
        currentIndex_ = index;
        if (onSectionSelected)
            onSectionSelected(index);
    });

    if (buttons_.size() == 1)
    {
        btn->setChecked(true);
        currentIndex_ = 0;
    }

    return index;
}

void LeftRail::setCurrentIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(buttons_.size()))
        return;
    currentIndex_ = index;
    buttons_[static_cast<size_t>(index)]->setChecked(true);
}

void LeftRail::setAccountGlyph(const QString& label, const QString& tooltip)
{
    accountBtn_->setText(label);
    accountBtn_->setToolTip(tooltip);
}

} // namespace jit::dash::ui
