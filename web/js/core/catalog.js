// Data-driven CK3 gene catalog, ported from src/core/GeneCatalog.cpp.

export class GeneCatalog {
  constructor() {
    this.genes = [];
    this.base64Order = [];
    this.indexByKey = new Map();
    this.categoryOrder = [];
  }

  static fromJson(root) {
    if (!root || typeof root !== 'object' || Array.isArray(root)) {
      throw new Error('Gene catalog is not a JSON object');
    }

    const catalog = new GeneCatalog();
    const categoryArray = Array.isArray(root.categoryOrder) ? root.categoryOrder : [];
    for (const value of categoryArray) {
      if (typeof value === 'string' && value && !catalog.categoryOrder.includes(value)) {
        catalog.categoryOrder.push(value);
      }
    }

    const genes = Array.isArray(root.genes) ? root.genes : [];
    const seenKeys = new Set();
    const seenBase64 = new Set();

    for (const value of genes) {
      if (!value || typeof value !== 'object' || Array.isArray(value)) {
        throw new Error('Gene catalog contains a non-object gene entry');
      }

      const gene = {
        key: typeof value.key === 'string' ? value.key : '',
        name: typeof value.name === 'string' ? value.name : '',
        category: typeof value.category === 'string' ? value.category : '',
        kind: typeof value.kind === 'string' ? value.kind : '',
        inBase64: value.inBase64 === true,
        base64Index: Number.isInteger(value.base64Index) ? value.base64Index : -1,
        options: [],
      };

      if (!gene.key) throw new Error('Gene catalog contains an entry without a key');
      if (!gene.name) gene.name = gene.key;
      if (seenKeys.has(gene.key)) throw new Error(`Duplicate gene key in catalog: ${gene.key}`);
      seenKeys.add(gene.key);

      const optionArray = Array.isArray(value.options) ? value.options : [];
      for (const optionValue of optionArray) {
        if (!optionValue || typeof optionValue !== 'object') continue;
        const option = {
          id: typeof optionValue.id === 'string' ? optionValue.id : '',
          index: Number.isInteger(optionValue.index) ? optionValue.index : 0,
          label: typeof optionValue.label === 'string' ? optionValue.label : '',
        };
        if (!option.id) continue;
        if (!option.label) option.label = option.id;
        gene.options.push(option);
      }
      gene.options.sort((a, b) => a.index - b.index);

      if (gene.inBase64) {
        if (gene.base64Index < 0 || seenBase64.has(gene.base64Index)) {
          throw new Error(`Invalid or duplicate Base64 index for ${gene.key}`);
        }
        seenBase64.add(gene.base64Index);
        catalog.base64Order.push(catalog.genes.length);
      }

      catalog.indexByKey.set(gene.key, catalog.genes.length);
      catalog.genes.push(gene);
    }

    if (catalog.genes.length === 0) throw new Error('Gene catalog is empty');

    catalog.base64Order.sort(
      (a, b) => catalog.genes[a].base64Index - catalog.genes[b].base64Index,
    );
    for (let i = 0; i < catalog.base64Order.length; i += 1) {
      if (catalog.genes[catalog.base64Order[i]].base64Index !== i) {
        throw new Error(`Base64 gene indexes are not contiguous (slot ${i})`);
      }
    }

    return catalog;
  }

  byKey(key) {
    const index = this.indexByKey.get(key);
    return index === undefined ? null : this.genes[index];
  }

  byBase64Index(index) {
    if (index < 0 || index >= this.base64Order.length) return null;
    return this.genes[this.base64Order[index]];
  }

  indexOfKey(key) {
    const index = this.indexByKey.get(key);
    return index === undefined ? -1 : index;
  }

  base64GeneCount() {
    return this.base64Order.length;
  }

  categoriesInUse() {
    const result = [];
    for (const category of this.categoryOrder) {
      if (this.genes.some((gene) => gene.category === category)) result.push(category);
    }
    for (const gene of this.genes) {
      if (gene.category && !result.includes(gene.category)) result.push(gene.category);
    }
    return result;
  }

  optionByIndex(key, index) {
    const gene = this.byKey(key);
    if (!gene) return null;
    return gene.options.find((option) => option.index === index) || null;
  }

  optionIndexById(key, id) {
    const gene = this.byKey(key);
    if (!gene) return -1;
    const option = gene.options.find((candidate) => candidate.id === id);
    return option ? option.index : -1;
  }

  optionLabel(key, index) {
    const option = this.optionByIndex(key, index);
    return option ? option.label : `Unknown (${index})`;
  }

  optionId(key, index) {
    const option = this.optionByIndex(key, index);
    return option ? option.id : '';
  }

  displayName(key) {
    const gene = this.byKey(key);
    return gene ? gene.name : key;
  }

  static categoryTranslationKey(category) {
    return `genes.category.${category}`;
  }
}
