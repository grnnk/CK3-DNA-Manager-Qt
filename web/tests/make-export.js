const fs=require('fs');
const dna=Buffer.alloc(416).toString('base64');
const png='iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAFgwJ/lK3P9wAAAABJRU5ErkJggg==';
const root={format:'ck3-dna-manager-qt',version:1,exportedAt:new Date().toISOString(),entries:[{name:'JSON Import Test',notes:'Imported from JSON',favorite:false,base64:dna,geneCount:104,moddedGeneCount:0,createdAt:1600000000000,updatedAt:1600000001000,portraitPng:png}]};
fs.writeFileSync('web/tests/fixtures/export.json',JSON.stringify(root,null,2));
console.log('ok');
