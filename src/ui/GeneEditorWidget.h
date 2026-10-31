#pragma once

#include "core/DnaData.h"

#include <QWidget>
#include <QVector>

class I18n;
class GeneCatalog;
struct GeneDefinition;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QToolButton;
class QVBoxLayout;
class QFrame;
class QLayout;

class GeneEditorWidget : public QWidget
{
    Q_OBJECT

public:
    GeneEditorWidget(const GeneCatalog &catalog, I18n &i18n, QWidget *parent = nullptr);

    void setData(const DnaData &data);
    DnaData data() const;
    void markSaved();
    void revert();
    bool hasUnsavedChanges() const { return m_dirty; }

    void setEntryAvailable(bool available);
    void retranslateUi();

signals:
    void saveRequested();
    void revertRequested();
    void dirtyChanged(bool dirty);

private:
    struct GeneRow
    {
        const GeneDefinition *definition = nullptr;
        QFrame *frame = nullptr;
        QComboBox *firstVariant = nullptr;
        QSpinBox *firstStrength = nullptr;
        QComboBox *secondVariant = nullptr;
        QSpinBox *secondStrength = nullptr;
        QSpinBox *colorValues[4] = { nullptr, nullptr, nullptr, nullptr };
    };

    struct ExtraRow
    {
        QString key;
        QString firstOption;
        QString secondOption;
        ExtraGeneKind kind = ExtraGeneKind::Modded;
        QFrame *frame = nullptr;
        QLabel *nameLabel = nullptr;
        QLabel *kindLabel = nullptr;
        QLabel *firstOptionLabel = nullptr;
        QLabel *secondOptionLabel = nullptr;
        QSpinBox *firstVariant = nullptr;
        QSpinBox *firstStrength = nullptr;
        QSpinBox *secondVariant = nullptr;
        QSpinBox *secondStrength = nullptr;
    };

    struct Section
    {
        QString category;
        QToolButton *header = nullptr;
        QWidget *container = nullptr;
        QVector<int> rowIndices;
    };

    void buildUi();
    void buildSections();
    void buildExtraSection();
    void rebuildExtras(const QVector<ExtraGene> &extras);
    void addGeneRow(int categoryIndex, const GeneDefinition &definition, Section &section);
    void populateVariantCombo(QComboBox *combo, const GeneDefinition &definition, int value);
    void applyFilter();
    void markDirty();
    void updateDirtyUi();
    void clearLayout(QLayout *layout);
    QString categoryLabel(const QString &category) const;
    void updateSectionTitles();

    const GeneCatalog &m_catalog;
    I18n &m_i18n;
    DnaData m_original;
    RulerMetadata m_ruler;

    QLineEdit *m_search = nullptr;
    QComboBox *m_category = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_legend = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_content = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    QLabel *m_noMatches = nullptr;

    QLabel *m_dirtyLabel = nullptr;
    QPushButton *m_revertButton = nullptr;
    QPushButton *m_saveButton = nullptr;

    QVector<GeneRow> m_rows;
    QVector<Section> m_sections;
    QVector<ExtraRow> m_extraRows;
    QVector<QString> m_categories;

    QFrame *m_extraFrame = nullptr;
    QToolButton *m_extraHeader = nullptr;
    QWidget *m_extraContainer = nullptr;
    QLabel *m_extraHint = nullptr;
    QVBoxLayout *m_extraLayout = nullptr;

    bool m_loading = false;
    bool m_dirty = false;
    bool m_entryAvailable = false;
};
