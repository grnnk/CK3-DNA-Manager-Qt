#include "ui/GeneEditorWidget.h"

#include "core/GeneCatalog.h"
#include "core/I18n.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QSpinBox *makeByteSpin(QWidget *parent)
{
    auto *spin = new QSpinBox(parent);
    spin->setRange(0, 255);
    spin->setAlignment(Qt::AlignCenter);
    spin->setFixedWidth(64);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    return spin;
}

} // namespace

GeneEditorWidget::GeneEditorWidget(const GeneCatalog &catalog, I18n &i18n, QWidget *parent)
    : QWidget(parent)
    , m_catalog(catalog)
    , m_i18n(i18n)
{
    buildUi();
}

void GeneEditorWidget::buildUi()
{
    m_loading = true;

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(18, 14, 18, 8);
    toolbarLayout->setSpacing(10);

    m_search = new QLineEdit(toolbar);
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(220);
    m_category = new QComboBox(toolbar);
    m_category->setMinimumWidth(180);
    m_countLabel = new QLabel(toolbar);
    m_countLabel->setObjectName(QStringLiteral("mutedLabel"));

    toolbarLayout->addWidget(m_search, 1);
    toolbarLayout->addWidget(m_category);
    toolbarLayout->addWidget(m_countLabel);
    root->addWidget(toolbar);

    m_legend = new QLabel(this);
    m_legend->setObjectName(QStringLiteral("mutedLabel"));
    m_legend->setWordWrap(true);
    m_legend->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_legend->setContentsMargins(18, 0, 18, 8);
    root->addWidget(m_legend);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget(m_scrollArea);
    m_content->setObjectName(QStringLiteral("geneContent"));
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(14, 4, 14, 14);
    m_contentLayout->setSpacing(2);
    m_scrollArea->setWidget(m_content);
    root->addWidget(m_scrollArea, 1);

    m_noMatches = new QLabel(m_content);
    m_noMatches->setObjectName(QStringLiteral("mutedLabel"));
    m_noMatches->setAlignment(Qt::AlignCenter);
    m_noMatches->setContentsMargins(0, 24, 0, 24);
    m_noMatches->hide();

    buildSections();
    buildExtraSection();
    m_contentLayout->addWidget(m_noMatches);
    m_contentLayout->addStretch(1);

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("editorFooter"));
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(18, 10, 18, 10);
    footerLayout->setSpacing(10);

    m_dirtyLabel = new QLabel(footer);
    m_dirtyLabel->setObjectName(QStringLiteral("dirtyLabel"));
    footerLayout->addWidget(m_dirtyLabel);
    footerLayout->addStretch(1);

    m_revertButton = new QPushButton(footer);
    m_revertButton->setObjectName(QStringLiteral("secondaryButton"));
    m_saveButton = new QPushButton(footer);
    m_saveButton->setObjectName(QStringLiteral("primaryButton"));
    footerLayout->addWidget(m_revertButton);
    footerLayout->addWidget(m_saveButton);
    root->addWidget(footer);

    connect(m_search, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(m_category, &QComboBox::currentIndexChanged, this, [this] { applyFilter(); });
    connect(m_saveButton, &QPushButton::clicked, this, &GeneEditorWidget::saveRequested);
    connect(m_revertButton, &QPushButton::clicked, this, &GeneEditorWidget::revertRequested);

    m_loading = false;
    retranslateUi();
    updateDirtyUi();
}

void GeneEditorWidget::buildSections()
{
    for (const QString &category : m_catalog.categoryOrder()) {
        QVector<const GeneDefinition *> definitions;
        for (const GeneDefinition &gene : m_catalog.genes()) {
            if (gene.inBase64 && gene.category == category)
                definitions.append(&gene);
        }
        if (definitions.isEmpty())
            continue;

        Section section;
        section.category = category;

        section.header = new QToolButton(m_content);
        section.header->setObjectName(QStringLiteral("sectionHeader"));
        section.header->setCheckable(true);
        section.header->setChecked(true);
        section.header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        section.header->setArrowType(Qt::DownArrow);
        section.header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        section.header->setCursor(Qt::PointingHandCursor);
        m_contentLayout->addWidget(section.header);

        section.container = new QWidget(m_content);
        section.container->setObjectName(QStringLiteral("sectionBody"));
        auto *sectionLayout = new QVBoxLayout(section.container);
        sectionLayout->setContentsMargins(0, 0, 0, 10);
        sectionLayout->setSpacing(0);

        QWidget *container = section.container;
        QToolButton *header = section.header;
        connect(header, &QToolButton::toggled, this, [header, container](bool expanded) {
            container->setVisible(expanded);
            header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        });

        m_contentLayout->addWidget(section.container);

        for (const GeneDefinition *definition : definitions)
            addGeneRow(m_sections.size(), *definition, section);

        m_sections.append(section);
        m_categories.append(category);
    }
}

void GeneEditorWidget::addGeneRow(int categoryIndex,
                                  const GeneDefinition &definition,
                                  Section &section)
{
    Q_UNUSED(categoryIndex)

    GeneRow row;
    row.definition = &definition;
    const QString geneKey = definition.key;
    const auto markEdited = [this, geneKey] {
        m_ruler.rawTokens.remove(geneKey);
        markDirty();
    };

    auto *frame = new QFrame(m_content);
    frame->setObjectName(QStringLiteral("geneRow"));
    auto *layout = new QHBoxLayout(frame);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(8);

    auto *labelBox = new QWidget(frame);
    labelBox->setFixedWidth(210);
    auto *labelLayout = new QVBoxLayout(labelBox);
    labelLayout->setContentsMargins(0, 0, 0, 0);
    labelLayout->setSpacing(0);

    auto *nameLabel = new QLabel(definition.name, labelBox);
    nameLabel->setObjectName(QStringLiteral("geneName"));
    auto *keyLabel = new QLabel(definition.key, labelBox);
    keyLabel->setObjectName(QStringLiteral("geneKey"));
    labelLayout->addWidget(nameLabel);
    labelLayout->addWidget(keyLabel);
    layout->addWidget(labelBox);

    if (definition.isColor()) {
        auto *tag = new QLabel(frame);
        tag->setObjectName(QStringLiteral("alleleTag"));
        layout->addWidget(tag);
        layout->addStretch(1);
        for (int i = 0; i < 4; ++i) {
            row.colorValues[i] = makeByteSpin(frame);
            layout->addWidget(row.colorValues[i]);
            connect(row.colorValues[i], &QSpinBox::valueChanged, this, markEdited);
        }
    } else {
        auto *firstTag = new QLabel(QStringLiteral("A1"), frame);
        firstTag->setObjectName(QStringLiteral("alleleTag"));
        firstTag->setFixedWidth(22);
        row.firstVariant = new QComboBox(frame);
        row.firstVariant->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        row.firstVariant->setMinimumContentsLength(8);
        row.firstVariant->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row.firstStrength = makeByteSpin(frame);

        auto *secondTag = new QLabel(QStringLiteral("A2"), frame);
        secondTag->setObjectName(QStringLiteral("alleleTag"));
        secondTag->setFixedWidth(22);
        row.secondVariant = new QComboBox(frame);
        row.secondVariant->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        row.secondVariant->setMinimumContentsLength(8);
        row.secondVariant->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row.secondStrength = makeByteSpin(frame);

        layout->addWidget(firstTag);
        layout->addWidget(row.firstVariant, 1);
        layout->addWidget(row.firstStrength);
        layout->addSpacing(6);
        layout->addWidget(secondTag);
        layout->addWidget(row.secondVariant, 1);
        layout->addWidget(row.secondStrength);

        for (QComboBox *combo : { row.firstVariant, row.secondVariant }) {
            connect(combo, &QComboBox::currentIndexChanged, this, markEdited);
        }
        for (QSpinBox *spin : { row.firstStrength, row.secondStrength }) {
            connect(spin, &QSpinBox::valueChanged, this, markEdited);
        }
        populateVariantCombo(row.firstVariant, definition, 0);
        populateVariantCombo(row.secondVariant, definition, 0);
    }

    frame->setToolTip(QStringLiteral("%1\n%2").arg(definition.name, definition.key));
    QVBoxLayout *sectionLayout = qobject_cast<QVBoxLayout *>(section.container->layout());
    sectionLayout->addWidget(frame);
    section.rowIndices.append(m_rows.size());
    row.frame = frame;
    m_rows.append(row);
}

void GeneEditorWidget::buildExtraSection()
{
    m_extraHeader = new QToolButton(m_content);
    m_extraHeader->setObjectName(QStringLiteral("sectionHeader"));
    m_extraHeader->setCheckable(true);
    m_extraHeader->setChecked(true);
    m_extraHeader->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_extraHeader->setArrowType(Qt::DownArrow);
    m_extraHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_extraHeader->setCursor(Qt::PointingHandCursor);
    m_extraHeader->hide();
    m_contentLayout->addWidget(m_extraHeader);

    m_extraContainer = new QWidget(m_content);
    m_extraContainer->setObjectName(QStringLiteral("sectionBody"));
    auto *layout = new QVBoxLayout(m_extraContainer);
    layout->setContentsMargins(0, 0, 0, 10);
    layout->setSpacing(0);
    m_extraHint = new QLabel(m_extraContainer);
    m_extraHint->setObjectName(QStringLiteral("mutedLabel"));
    m_extraHint->setWordWrap(true);
    m_extraHint->setContentsMargins(12, 2, 12, 8);
    layout->addWidget(m_extraHint);
    m_extraLayout = new QVBoxLayout();
    m_extraLayout->setContentsMargins(0, 0, 0, 0);
    m_extraLayout->setSpacing(0);
    layout->addLayout(m_extraLayout);
    m_extraContainer->hide();
    m_contentLayout->addWidget(m_extraContainer);

    connect(m_extraHeader, &QToolButton::toggled, this,
            [this](bool expanded) {
                m_extraContainer->setVisible(expanded && !m_extraRows.isEmpty());
                m_extraHeader->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            });
}

void GeneEditorWidget::populateVariantCombo(QComboBox *combo,
                                            const GeneDefinition &definition,
                                            int value)
{
    QSignalBlocker blocker(combo);
    combo->clear();
    bool found = false;
    for (const GeneOption &option : definition.options) {
        combo->addItem(option.label, option.index);
        combo->setItemData(combo->count() - 1, option.id, Qt::ToolTipRole);
        if (option.index == value)
            found = true;
    }
    if (!found) {
        combo->addItem(m_i18n.t(QStringLiteral("genes.unknownOption")).arg(value), value);
    }
    const int index = combo->findData(value);
    if (index >= 0)
        combo->setCurrentIndex(index);
}

void GeneEditorWidget::setData(const DnaData &data)
{
    m_loading = true;

    for (GeneRow &row : m_rows) {
        if (!row.definition)
            continue;
        if (row.definition->isColor()) {
            const std::array<quint8, 4> values = data.color(row.definition->key);
            for (int i = 0; i < 4; ++i) {
                QSignalBlocker blocker(row.colorValues[i]);
                row.colorValues[i]->setValue(values[i]);
            }
        } else {
            const GenePair pair = data.gene(row.definition->key);
            for (int i = 0; i < 2; ++i) {
                QComboBox *combo = i == 0 ? row.firstVariant : row.secondVariant;
                QSpinBox *strength = i == 0 ? row.firstStrength : row.secondStrength;
                populateVariantCombo(combo, *row.definition,
                                     i == 0 ? pair.first.variant : pair.second.variant);
                QSignalBlocker blocker(strength);
                strength->setValue(i == 0 ? pair.first.intensity : pair.second.intensity);
            }
        }
    }

    m_loading = false;
    m_ruler = data.ruler;
    rebuildExtras(data.extras);
    m_original = data;
    m_dirty = false;
    updateDirtyUi();
    applyFilter();
    emit dirtyChanged(false);
}

DnaData GeneEditorWidget::data() const
{
    DnaData result;
    result.ruler = m_ruler;
    for (const GeneRow &row : m_rows) {
        if (!row.definition)
            continue;
        if (row.definition->isColor()) {
            std::array<quint8, 4> values {};
            for (int i = 0; i < 4; ++i)
                values[i] = static_cast<quint8>(row.colorValues[i]->value());
            result.setColor(row.definition->key, values);
        } else {
            GenePair pair;
            pair.first.variant = row.firstVariant->currentData().toInt();
            pair.first.intensity = row.firstStrength->value();
            pair.second.variant = row.secondVariant->currentData().toInt();
            pair.second.intensity = row.secondStrength->value();
            result.setGene(row.definition->key, pair);
        }
    }

    for (const ExtraRow &row : m_extraRows) {
        ExtraGene extra;
        extra.key = row.key;
        extra.kind = row.kind;
        extra.bytes[0] = static_cast<quint8>(row.firstOption.isEmpty()
                                                 ? row.firstVariant->value()
                                                 : 0);
        extra.bytes[1] = static_cast<quint8>(row.firstStrength->value());
        extra.bytes[2] = static_cast<quint8>(row.secondOption.isEmpty()
                                                 ? row.secondVariant->value()
                                                 : 0);
        extra.bytes[3] = static_cast<quint8>(row.secondStrength->value());
        extra.firstOption = row.firstOption;
        extra.secondOption = row.secondOption;
        result.extras.append(extra);
    }
    return result;
}

void GeneEditorWidget::markSaved()
{
    m_original = data();
    m_dirty = false;
    updateDirtyUi();
    emit dirtyChanged(false);
}

void GeneEditorWidget::revert()
{
    setData(m_original);
}

void GeneEditorWidget::markDirty()
{
    if (m_loading || !m_entryAvailable)
        return;
    if (m_dirty)
        return;
    m_dirty = true;
    updateDirtyUi();
    emit dirtyChanged(true);
}

void GeneEditorWidget::updateDirtyUi()
{
    if (!m_dirtyLabel)
        return;
    m_dirtyLabel->setText(m_dirty ? m_i18n.t(QStringLiteral("genes.unsaved"))
                                  : m_i18n.t(QStringLiteral("genes.noChanges")));
    m_dirtyLabel->setProperty("dirty", m_dirty);
    m_dirtyLabel->style()->unpolish(m_dirtyLabel);
    m_dirtyLabel->style()->polish(m_dirtyLabel);
    m_saveButton->setEnabled(m_entryAvailable && m_dirty);
    m_revertButton->setEnabled(m_entryAvailable && m_dirty);
}

void GeneEditorWidget::setEntryAvailable(bool available)
{
    m_entryAvailable = available;
    m_search->setEnabled(available);
    m_category->setEnabled(available);
    m_scrollArea->setEnabled(available);
    updateDirtyUi();
}

void GeneEditorWidget::clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            delete widget;
        delete item;
    }
}

void GeneEditorWidget::rebuildExtras(const QVector<ExtraGene> &extras)
{
    clearLayout(m_extraLayout);
    m_extraRows.clear();
    m_extraRows.reserve(extras.size());

    const bool hasExtras = !extras.isEmpty();
    m_extraHeader->setVisible(hasExtras);
    m_extraContainer->setVisible(hasExtras && m_extraHeader->isChecked());

    for (int index = 0; index < extras.size(); ++index) {
        const ExtraGene &extra = extras.at(index);

        ExtraRow row;
        row.key = extra.key;
        row.firstOption = extra.firstOption;
        row.secondOption = extra.secondOption;
        row.kind = extra.kind;

        auto *frame = new QFrame(m_extraContainer);
        frame->setObjectName(QStringLiteral("geneRow"));
        row.frame = frame;
        auto *layout = new QHBoxLayout(frame);
        layout->setContentsMargins(10, 6, 10, 6);
        layout->setSpacing(8);

        auto *labelBox = new QWidget(frame);
        labelBox->setFixedWidth(210);
        auto *labelLayout = new QVBoxLayout(labelBox);
        labelLayout->setContentsMargins(0, 0, 0, 0);
        labelLayout->setSpacing(0);
        row.nameLabel = new QLabel(labelBox);
        row.nameLabel->setObjectName(QStringLiteral("geneName"));
        row.kindLabel = new QLabel(labelBox);
        row.kindLabel->setObjectName(QStringLiteral("geneKey"));
        row.kindLabel->setText(m_i18n.t(row.kind == ExtraGeneKind::RulerOnly
                                           ? QStringLiteral("genes.rulerOnlyKind")
                                           : QStringLiteral("genes.moddedKind")));
        if (extra.key.startsWith(QStringLiteral("extra_"))) {
            bool ok = false;
            const int slot = extra.key.mid(6).toInt(&ok);
            row.nameLabel->setText(m_i18n.t(QStringLiteral("genes.extraGene"))
                                       .arg(ok ? slot : index + 1));
        } else {
            row.nameLabel->setText(extra.key);
        }
        labelLayout->addWidget(row.nameLabel);
        labelLayout->addWidget(row.kindLabel);
        layout->addWidget(labelBox);

        auto *firstTag = new QLabel(QStringLiteral("A1"), frame);
        firstTag->setObjectName(QStringLiteral("alleleTag"));
        firstTag->setFixedWidth(22);
        row.firstVariant = makeByteSpin(frame);
        row.firstStrength = makeByteSpin(frame);
        auto *secondTag = new QLabel(QStringLiteral("A2"), frame);
        secondTag->setObjectName(QStringLiteral("alleleTag"));
        secondTag->setFixedWidth(22);
        row.secondVariant = makeByteSpin(frame);
        row.secondStrength = makeByteSpin(frame);

        row.firstOptionLabel = new QLabel(extra.firstOption, frame);
        row.firstOptionLabel->setObjectName(QStringLiteral("geneKey"));
        row.firstOptionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row.firstOptionLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        row.firstOptionLabel->setToolTip(extra.firstOption);
        row.firstOptionLabel->setVisible(!extra.firstOption.isEmpty());
        row.firstVariant->setVisible(extra.firstOption.isEmpty());
        row.secondOptionLabel = new QLabel(extra.secondOption, frame);
        row.secondOptionLabel->setObjectName(QStringLiteral("geneKey"));
        row.secondOptionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row.secondOptionLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        row.secondOptionLabel->setToolTip(extra.secondOption);
        row.secondOptionLabel->setVisible(!extra.secondOption.isEmpty());
        row.secondVariant->setVisible(extra.secondOption.isEmpty());

        row.firstVariant->setToolTip(m_i18n.t(QStringLiteral("genes.variant")));
        row.secondVariant->setToolTip(m_i18n.t(QStringLiteral("genes.variant")));
        row.firstStrength->setToolTip(m_i18n.t(QStringLiteral("genes.strength")));
        row.secondStrength->setToolTip(m_i18n.t(QStringLiteral("genes.strength")));

        layout->addWidget(firstTag);
        layout->addWidget(row.firstOptionLabel, 1);
        layout->addWidget(row.firstVariant);
        layout->addWidget(row.firstStrength);
        layout->addSpacing(6);
        layout->addWidget(secondTag);
        layout->addWidget(row.secondOptionLabel, 1);
        layout->addWidget(row.secondVariant);
        layout->addWidget(row.secondStrength);
        layout->addStretch(1);

        const QString firstValue = extra.firstOption.isEmpty()
            ? QString::number(static_cast<int>(extra.bytes[0]))
            : extra.firstOption;
        const QString secondValue = extra.secondOption.isEmpty()
            ? QString::number(static_cast<int>(extra.bytes[2]))
            : extra.secondOption;
        frame->setToolTip(QStringLiteral("%1\n%2\nA1: %3 (%4)\nA2: %5 (%6)")
                              .arg(extra.key,
                                   m_i18n.t(extra.kind == ExtraGeneKind::RulerOnly
                                                ? QStringLiteral("genes.rulerOnlyKind")
                                                : QStringLiteral("genes.moddedKind")),
                                   firstValue,
                                   QString::number(static_cast<int>(extra.bytes[1])),
                                   secondValue,
                                   QString::number(static_cast<int>(extra.bytes[3]))));
        m_extraLayout->addWidget(frame);
        m_extraRows.append(row);
        const int rowIndex = m_extraRows.size() - 1;

        connect(row.firstVariant, &QSpinBox::valueChanged, this, [this, rowIndex] {
            if (rowIndex < m_extraRows.size()) {
                m_extraRows[rowIndex].firstOption.clear();
                m_ruler.rawTokens.remove(m_extraRows[rowIndex].key);
            }
            markDirty();
        });
        connect(row.secondVariant, &QSpinBox::valueChanged, this, [this, rowIndex] {
            if (rowIndex < m_extraRows.size()) {
                m_extraRows[rowIndex].secondOption.clear();
                m_ruler.rawTokens.remove(m_extraRows[rowIndex].key);
            }
            markDirty();
        });
        connect(row.firstStrength, &QSpinBox::valueChanged, this, [this, rowIndex] {
            if (rowIndex < m_extraRows.size())
                m_ruler.rawTokens.remove(m_extraRows[rowIndex].key);
            markDirty();
        });
        connect(row.secondStrength, &QSpinBox::valueChanged, this, [this, rowIndex] {
            if (rowIndex < m_extraRows.size())
                m_ruler.rawTokens.remove(m_extraRows[rowIndex].key);
            markDirty();
        });

        QSignalBlocker blocker1(row.firstVariant);
        QSignalBlocker blocker2(row.firstStrength);
        QSignalBlocker blocker3(row.secondVariant);
        QSignalBlocker blocker4(row.secondStrength);
        row.firstVariant->setValue(extra.bytes[0]);
        row.firstStrength->setValue(extra.bytes[1]);
        row.secondVariant->setValue(extra.bytes[2]);
        row.secondStrength->setValue(extra.bytes[3]);
    }

    updateSectionTitles();
    applyFilter();
}

QString GeneEditorWidget::categoryLabel(const QString &category) const
{
    return m_i18n.t(GeneCatalog::categoryTranslationKey(category));
}

void GeneEditorWidget::updateSectionTitles()
{
    for (Section &section : m_sections) {
        section.header->setText(QStringLiteral("%1  ·  %2")
                                    .arg(categoryLabel(section.category))
                                    .arg(section.rowIndices.size()));
    }

    if (m_extraHeader) {
        const QString text = m_i18n.t(QStringLiteral("genes.nonVanilla"));
        m_extraHeader->setText(QStringLiteral("%1  ·  %2").arg(text).arg(m_extraRows.size()));
    }

    for (int index = 0; index < m_extraRows.size(); ++index) {
        ExtraRow &row = m_extraRows[index];
        row.kindLabel->setText(m_i18n.t(row.kind == ExtraGeneKind::RulerOnly
                                           ? QStringLiteral("genes.rulerOnlyKind")
                                           : QStringLiteral("genes.moddedKind")));
        if (row.key.startsWith(QStringLiteral("extra_"))) {
            bool ok = false;
            const int slot = row.key.mid(6).toInt(&ok);
            row.nameLabel->setText(m_i18n.t(QStringLiteral("genes.extraGene"))
                                       .arg(ok ? slot : m_catalog.base64GeneCount() + index + 1));
        } else {
            row.nameLabel->setText(row.key);
        }
    }
}

void GeneEditorWidget::applyFilter()
{
    if (!m_search || !m_category)
        return;

    const QString query = m_search->text().trimmed();
    const QString category = m_category->currentData().toString();

    QVector<bool> matches(m_rows.size(), false);
    int visible = 0;
    for (int i = 0; i < m_rows.size(); ++i) {
        const GeneDefinition *definition = m_rows.at(i).definition;
        if (!definition)
            continue;
        const bool categoryMatch = category.isEmpty() || definition->category == category;
        const bool textMatch = query.isEmpty()
            || definition->name.contains(query, Qt::CaseInsensitive)
            || definition->key.contains(query, Qt::CaseInsensitive);
        matches[i] = categoryMatch && textMatch;
        m_rows[i].frame->setVisible(matches.at(i));
        if (matches.at(i))
            ++visible;
    }

    for (Section &section : m_sections) {
        bool any = false;
        for (int rowIndex : section.rowIndices) {
            if (rowIndex >= 0 && rowIndex < matches.size() && matches.at(rowIndex)) {
                any = true;
                break;
            }
        }
        section.header->setVisible(any);
        section.container->setVisible(any && section.header->isChecked());
    }

    int visibleExtras = 0;
    if (!m_extraRows.isEmpty()) {
        const bool categoryAllows = category.isEmpty();
        for (ExtraRow &row : m_extraRows) {
            const QString displayName = row.nameLabel->text();
            const bool textMatch = query.isEmpty()
                || row.key.contains(query, Qt::CaseInsensitive)
                || displayName.contains(query, Qt::CaseInsensitive);
            const bool show = categoryAllows && textMatch;
            row.frame->setVisible(show);
            if (show)
                ++visibleExtras;
        }
        m_extraHeader->setVisible(categoryAllows && visibleExtras > 0);
        m_extraContainer->setVisible(categoryAllows && visibleExtras > 0
                                     && m_extraHeader->isChecked());
    }

    const int total = m_rows.size() + m_extraRows.size();
    const int shown = visible + visibleExtras;
    m_countLabel->setText(shown == total
        ? m_i18n.t(QStringLiteral("genes.count")).arg(shown)
        : m_i18n.t(QStringLiteral("genes.countFiltered")).arg(shown).arg(total));

    m_noMatches->setVisible(m_entryAvailable && shown == 0);
}

void GeneEditorWidget::retranslateUi()
{
    m_search->setPlaceholderText(m_i18n.t(QStringLiteral("genes.searchPlaceholder")));
    m_legend->setText(m_i18n.t(QStringLiteral("genes.hint")));
    m_saveButton->setText(m_i18n.t(QStringLiteral("genes.save")));
    m_revertButton->setText(m_i18n.t(QStringLiteral("genes.revert")));
    m_noMatches->setText(m_i18n.t(QStringLiteral("genes.noMatches")));
    m_extraHint->setText(m_i18n.t(QStringLiteral("genes.moddedHint")));

    for (QLabel *label : m_content->findChildren<QLabel *>(QStringLiteral("alleleTag"))) {
        if (label->text().isEmpty())
            label->setText(m_i18n.t(QStringLiteral("genes.colorValues")));
    }

    const QString selected = m_category->currentData().toString();
    {
        QSignalBlocker blocker(m_category);
        m_category->clear();
        m_category->addItem(m_i18n.t(QStringLiteral("genes.allCategories")), QString());
        for (const QString &category : m_categories)
            m_category->addItem(categoryLabel(category), category);
        const int index = m_category->findData(selected);
        m_category->setCurrentIndex(index >= 0 ? index : 0);
    }

    for (ExtraRow &row : m_extraRows) {
        if (row.kindLabel) {
            row.kindLabel->setText(m_i18n.t(row.kind == ExtraGeneKind::RulerOnly
                                                ? QStringLiteral("genes.rulerOnlyKind")
                                                : QStringLiteral("genes.moddedKind")));
        }
        row.firstVariant->setToolTip(m_i18n.t(QStringLiteral("genes.variant")));
        row.secondVariant->setToolTip(m_i18n.t(QStringLiteral("genes.variant")));
        row.firstStrength->setToolTip(m_i18n.t(QStringLiteral("genes.strength")));
        row.secondStrength->setToolTip(m_i18n.t(QStringLiteral("genes.strength")));
    }

    updateSectionTitles();
    updateDirtyUi();
    applyFilter();
}
