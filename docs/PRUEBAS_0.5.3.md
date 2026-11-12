# Pruebas manuales 0.5.3 · pantalla completa y presets

**[NO EJECUTADO] XP/Atom físicos en el entorno de build.** Usar APROBADO, FALLO,
BLOQUEADO o NO EJECUTADO; anotar Windows/SP, resolución, escala, CPU/RAM y si se
usa disco o pendrive. Una prueba en Windows moderno no completa XP/Atom.

Extraer todo `TubeLiviano_Portable_0.5.3_x86.zip` en una carpeta nueva, por ejemplo
`C:\TL053`, y abrir el EXE de la carpeta TubeLiviano. Confirmar **0.5.3** y el motor
correspondiente al OS. Desconectar la red para las pruebas de clips/presets. El
[checklist base](PRUEBAS_XP_ATOM.md) conserva pruebas de motores y mediciones;
al repetirlo con esta entrega, el título esperado es 0.5.3.

## Pantalla completa sin Internet

| ID | Pasos | Resultado esperado |
| --- | --- | --- |
| F1 | Ir a Resultados antes de reproducir. Recorrer con Tab. | Pantalla completa visible y deshabilitado; el foco de los controles se ve. |
| F2 | Probar imagen y audio con 240p. Antes de terminar los ocho segundos, pulsar Pantalla completa o F11. | Video en el monitor actual, sin bordes; barra inferior con Pausa, Cancelar y Volver. No empieza de nuevo el patrón/tono por cambiar de ventana. |
| F3 | Pulsar Esc una vez; entrar de nuevo y mantener Esc pulsado al salir. | Regresa al tamaño/posición previo; el video continúa. Mantener la tecla no lo cancela. Otra pulsación independiente de Esc en ventana normal cancela. |
| F4 | Pausar y entrar/salir de pantalla completa; luego reanudar. | Se conserva la pausa y continúa desde el mismo punto al reanudar. |
| F5 | Entrar con F11, hacer clic en el video y salir con Esc; repetir F11. Usar Alt+Tab hacia otra aplicación. | Atajos funcionan con foco en video; la otra aplicación recibe sus teclas cuando está activa. |
| F6 | Reproducir en pantalla completa y pulsar Cancelar. Repetir dejando terminar el clip. | Ambas situaciones restauran la vista normal; Cancelar detiene imagen y sonido. Un error de reproducción también debe dejar visibles sus avisos. |
| F7 | Maximizar la ventana, reproducir y entrar/salir de pantalla completa. Repetir desde una ventana normal de otro tamaño/posición. | Vuelve respectivamente a maximizada o al rectángulo normal anterior. |
| F8 | Activar Recordar tamaño. Desde ventana normal, entrar en pantalla completa y cerrar con Alt+F4. Abrir de nuevo. | Abre una ventana normal con el tamaño guardado antes de entrar; no guarda las dimensiones de pantalla completa como tamaño normal. |
| F9 | Repetir F2–F4 con 144p y 360p y confirmar sonido audible. | Los tres clips muestran patrón y tono; registrar cortes o errores por perfil. El batch con audio dummy no aprueba por sí solo los parlantes. |
| F10 | Cliente mínimo 660×450, texto grande; repetir con tema claro/oscuro. | Pantalla completa y Configuración accesibles en Resultados; barra de salida usable. Registrar recorte/solapamiento si existe. |

Con dos monitores, mover primero la ventana al segundo y repetir F2/F7: pantalla
completa debe ocupar ese monitor y restaurar la ventana anterior. Sin segundo
monitor: NO EJECUTADO. Sin herramienta para medir continuidad, describir el patrón
observado sin inventar contadores.

## Cinco presets y guardado

En Configuración → General, elegir un preset y **Preparar**; revisar Reproducción
y Red antes de guardar. El preset modifica estos cinco campos:

| Preset | Calidad | Búfer ms | Prefetch KiB | Hilos | Modo |
| --- | --- | ---: | ---: | ---: | --- |
| Mínimo | 144p | 250 | 64 | 1 | Directo |
| Atom | 240p | 450 | 128 | 1 | Directo |
| Conexión inestable | 240p | 1500 | 256 | 1 | Directo |
| 360p | 360p | 600 | 128 | 2 | Directo |
| Puente | 240p | 800 | 128 | 1 | Puente |

| ID | Pasos | Resultado esperado |
| --- | --- | --- |
| P1 | Abrir Configuración y cambiar solo la selección del preset, sin Preparar. | Los valores de Reproducción/Red conservan el borrador actual. |
| P2 | Preparar cada uno de los cinco y revisar los campos contra la tabla. | Coinciden calidad/búfer/prefetch/hilos/modo; la descripción del selector ayuda a anticiparlos. |
| P3 | Antes de preparar, cambiar volumen a 35, tema/texto, resultados a 6, un timeout y dirección con `?token=PRUEBA_053`. | Preparar conserva esos valores y la URL de sesión. Los cuatro primeros presets eligen Directo; Puente elige Puente. |
| P4 | Preparar Puente y pulsar Cancelar. Abrir Configuración otra vez. | El preset no se aplica al reproductor ni al INI; aparecen las preferencias activas anteriores. |
| P5 | Preparar Mínimo, Guardar, cerrar y abrir. Revisar Reproducción y Red. | Se restauran 144p/250/64/1/Directo. Las preferencias ajenas se conservan; el token no vuelve del INI. |
| P6 | Escribir volumen 101 y volver a General → Preparar. Corregir a 35. | Se informa el volumen inválido y permite corregirlo; no se reemplaza silenciosamente por un valor predeterminado. |
| P7 | Con app cerrada e INI existente, respaldarlo y marcarlo solo lectura. Abrir, preparar Atom y Guardar; cerrar/abrir. | Se aplica a la sesión con aviso de que no guardó; el archivo previo permanece. Al reiniciar vuelven sus valores persistidos. Quitar solo lectura/restaurar respaldo. |
| P8 | Tema claro/oscuro y texto grande, cliente 660×450; recorrer General con Tab. | Selector, Preparar, descripción, Guardar y Cancelar se ven y funcionan. |

Para P7, desde la carpeta del EXE y con la aplicación cerrada:

```bat
copy /Y TubeLiviano.ini TubeLiviano.ini.antes
attrib +R TubeLiviano.ini
rem Ejecutar P7 y cerrar antes de restaurar.
attrib -R TubeLiviano.ini
copy /Y TubeLiviano.ini.antes TubeLiviano.ini
del TubeLiviano.ini.antes
```

El preset Puente conserva la dirección, pero reproducir requiere el servidor de
la otra PC. Probarlo solo cuando esté disponible; su búsqueda sigue usando el
motor local. El preset de 360p/dos hilos es opcional: **Atom · 240p** sigue siendo
el punto inicial recomendado y Mínimo reduce resolución. Ninguno tiene fluidez o
RAM medidas en Atom real durante esta entrega.

## Registro para comparar

```text
Windows/SP/arquitectura, CPU/RAM, resolución/escala:
Versión 0.5.3 y motor mostrado:
F1–F10 / P1–P8: APROBADO, FALLO, BLOQUEADO o NO EJECUTADO
Perfil/clip y preset preparado/guardado:
Imagen, sonido, continuidad y errores exactos:
CPU/RAM observadas antes/durante/después:
Rectángulo/estado antes y después de pantalla completa:
Ruta del INI y resultado de guardado/reapertura:
```

Siguiente mejor acción en XP/Atom: F1–F4 y P2–P5 con clips offline, luego medir
240p sin cambiar el resto de condiciones. Conservar capturas y el texto exacto si
el framework XP o la salida de video fallan; no cambiar DLLs del sistema.
