// Lector falso sin nada sonando: deja solo la fuente que se está probando.
'use strict';
const emit = () => process.stdout.write('{"sessions":[]}\n');
emit();
setInterval(emit, 1000);
