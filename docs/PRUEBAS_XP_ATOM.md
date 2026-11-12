# Pruebas manuales de TubeLiviano 0.5.2 en XP y Atom

Este es el trabajo pendiente para una máquina física. **[NO EJECUTADO]** durante
la sesión de build: no hay XP ni Atom disponibles. [SUPUESTO] XP SP3 de 32 bits,
cuenta normal y Atom N270/512 MB cuando se mida el objetivo de rendimiento.
Una prueba exitosa en otro Windows no completa la columna XP/Atom.

Usá `APROBADO`, `FALLO`, `BLOQUEADO` o `NO EJECUTADO`. Un error HTTP/verificación
puede bloquear una prueba online; una DLL que no carga es un fallo de compatibilidad,
no un video aprobado. Guardá el texto/captura del error. Las pruebas offline deben
poder hacerse con la red desconectada.

## Preparación y ficha

1. Descargá `TubeLiviano_Portable_0.5.2_x86.zip` de `releases/`. Si hay herramienta
   SHA-256 disponible, compará con `releases/SHA256SUMS.txt`; si no, registrá
   `[NO EJECUTADO] SHA-256 en XP` y conservá el ZIP original.
2. Extraé completo a una carpeta nueva, por ejemplo `C:\TL052`. Los comandos de
   esta guía suponen que el EXE quedó en `C:\TL052\TubeLiviano`.
3. Cerrá otras instancias de TubeLiviano, yt-dlp y reproductores. No actives un
   modo de compatibilidad añadido para la prueba base.
4. Win+R → `winver`: anotá edición, versión, compilación y Service Pack. Abrí
   Administrador de tareas (Ctrl+Mayús+Esc) para observar procesos, CPU y memoria.

```text
Fecha / equipo:
Windows (texto de winver) / 32 o 64 bits / Service Pack:
CPU / RAM / resolución / DPI o escala:
Versión de TubeLiviano visible:
ZIP y SHA-256 verificado (sí/no):
Ubicación: disco o pendrive / sistema de archivos:
Cambios o extensiones del kernel conocidos:
Estado de cada prueba (APROBADO/FALLO/BLOQUEADO/NO EJECUTADO):
Errores exactos / JSON y carpeta de informes:
```

## A. Carga y funcionamiento sin Internet

| ID | Pasos exactos | Resultado esperado |
| --- | --- | --- |
| A1 | Desconectar red. Abrir `TubeLiviano.exe` de la carpeta nueva. | Título 0.5.2, Inicio oscuro, controles visibles; sin instalación/admin ni error DLL. En XP SP3 x86, etiqueta Windows 5.1 y motor XP. Anotar diferencias. |
| A2 | En Inicio pulsar Probar imagen y audio con 240p. Repetir con 144p y 360p. | Patrón animado y tono de 440 Hz en cada perfil. Anotar imagen, sonido y cortes por separado; «reproduciendo» solo no aprueba. |
| A3 | Durante el clip, pulsar Pausa, esperar dos segundos, pulsar Pausa otra vez y Cancelar. | Se pausa/reanuda; Cancelar detiene imagen/sonido y la ventana responde. |
| A4 | Ejecutar `Probar_offline.cmd` de la raíz extraída. Anotar carpeta que imprime. | Código 0, tres perfiles APROBADO y JSON de esta ejecución en `%TEMP%`. El script permite hasta 30 s por perfil. Su audio es dummy; A2 comprueba los parlantes. |
| A5 | Configuración → General: activar texto grande y tema claro, Guardar; repetir tema oscuro. | Todos los controles siguen utilizables; letra/colores cambian. |
| A6 | En Reproducción escribir búfer 249 y Guardar; corregir a 450. Repetir volumen 101→80. | 249 y 101 se rechazan con aviso y el diálogo permanece; los valores válidos se aceptan. |
| A7 | Abrir Configuración, cambiar un valor y pulsar Cancelar o Esc. | Se conserva el valor activo anterior; no se guarda el borrador. |
| A8 | Reducir al mínimo: cliente 660×450, aproximadamente 676×489 externo con bordes clásicos. Probar Inicio, Resultados y Configuración en ambos temas y texto grande. | Buscador, navegación, botones y avisos accesibles; no se pisan controles. En 1024×600 el diálogo entra en el área de trabajo. |
| A9 | Ctrl+F y escribir; Tab/Mayús+Tab recorre controles. Ctrl+, abre Configuración; Esc la cierra. En Resultados usar flechas, Tab hasta Reproducir y Espacio. | Foco visible y acciones correspondientes. Enter busca desde el buscador. Esc en la ventana principal cancela actividad. |
| A10 | Reproducir un clip, pulsar Inicio y luego Resultados. | Inicio detiene/cancela y conserva contexto. Volver no lanza video automáticamente; cambiar calidad en Inicio tampoco. |

## B. Configuración, carpeta portable y privacidad

| ID | Pasos exactos | Resultado esperado |
| --- | --- | --- |
| B1 | Elegir tema claro, texto grande, 6 resultados y 144p/250 ms/64 KiB/un hilo/volumen 35. Guardar, cerrar y abrir. | Se restauran desde `TubeLiviano.ini` junto al EXE; Configuración muestra esos valores. |
| B2 | Activar Recordar tamaño, Guardar, cambiar tamaño normal, cerrar y abrir. Desactivar y repetir. | Activado restaura dimensiones; desactivado usa tamaño inicial. No se promete restaurar posición. |
| B3 | Preparar y Guardar Atom · 240p; revisar Reproducción. Preparar Mínimo · 144p. | Atom: 240/450/128/1; Mínimo: 144/250/64/1. Tema, volumen, límites y timeouts no cambian por esos presets. Restablecer prepara valores iniciales y requiere Guardar. |
| B4 | En Red escribir `http://192.168.1.20:8787?token=PRUEBA_LOCAL_052`, Guardar. Cerrar, abrir INI con Bloc de notas y reiniciar. | El token no aparece en BridgeURL ni vuelve al reiniciar; se conserva la dirección sin consulta. No se necesita servidor para comprobar persistencia. |
| B4b | En la dirección del puente de la ventana principal escribir una URL distinta, sin cambiar el foco; abrir Configuración con Ctrl+, y revisar Red. Cancelar, editar otra dirección y cerrar con Alt+F4 sin cambiar el foco; abrir otra vez. | Red toma el texto recién escrito; al reiniciar se conserva la última dirección, sin token. |
| B5 | Con INI creado y app cerrada, respaldarlo y ejecutar `attrib +R TubeLiviano.ini`. Cambiar tema en Configuración y Guardar; cerrar/abrir. | Se aplica en la sesión y avisa que no pudo guardar. El archivo previo no cambia; al reiniciar vuelve su valor persistido. Restaurar atributos y copia al terminar. |
| B6 | Copiar toda la carpeta a un pendrive. Desde `C:\Windows` ejecutar `"E:\TL052\TubeLiviano\TubeLiviano.exe"`. Repetir offline. | Encuentra DLLs, motores, clips e INI respecto al EXE, independientemente del directorio de trabajo. No copiar solo el EXE. |

Comandos de B5, desde la carpeta del EXE, después de crear el INI:

```bat
copy /Y TubeLiviano.ini TubeLiviano.ini.antes
attrib +R TubeLiviano.ini
rem Ejecutar B5 y cerrar TubeLiviano antes de restaurar.
attrib -R TubeLiviano.ini
copy /Y TubeLiviano.ini.antes TubeLiviano.ini
del TubeLiviano.ini.antes
```

En un pendrive protegido físicamente o carpeta sin permiso de escritura, la
aplicación debe permitir la prueba offline y cambios de sesión. Registrá el aviso;
no cuentes como persistidos cambios que no pudieron escribirse.

## C. Motor XP, búsqueda y enlaces

En una copia de prueba de la carpeta portable, abrir CMD allí y ejecutar:

```bat
engines\xp\yt-dlp.exe --version > motor-xp.txt 2>&1
echo Codigo=%ERRORLEVEL% >> motor-xp.txt
```

Esperado: versión impresa y código 0. Si aparece error de carga, anotá también el
texto del diálogo y detené las pruebas online del motor: **FALLO**, aunque la
interfaz y VLC pasen. No reemplaces el motor XP por el moderno ni cambies DLLs del
sistema para ocultar el resultado.

| ID | Pasos exactos | Resultado esperado |
| --- | --- | --- |
| C1 | Cerrar app, renombrar `engines\xp` a `engines\xp_apartado`. Abrir y buscar; cerrar y restaurar nombre. | Error claro de motor XP ausente. No inicia el moderno como sustituto. Offline sigue disponible. |
| C2 | Con red, buscar un título conocido con 6 resultados y luego 12. | Hasta el límite elegido; filas con título y canal/duración, sin perder caracteres. 403/429/verificación se registra como BLOQUEADO online. |
| C3 | Seleccionar y reproducir; borrar buscador, cambiar calidad y usar Reintentar. Repetir pegando URL. | Conserva/re-resuelve el enlace elegido sin pegarlo otra vez. No ofrece calidades altas. |
| C4 | Iniciar resolución y Cancelar cuando aparezca yt-dlp. Observar procesos; repetir cerrando la ventana. | Terminan todos los `yt-dlp.exe`/`qjs.exe` de esta instancia y la UI responde. Medir tiempo: objetivo de comprobación ≤5 s. One-file puede usar más de un proceso con el mismo nombre. |
| C5 | Durante resolución/reproducción ir a Inicio; cambiar calidad, volver a Resultados y Reproducir. | Inicio detiene/cancela; la nueva calidad queda preparada. No reaparece audio ni video por cambiar el selector en Inicio. |
| C6 | Preparar resolución de 30 s y una condición de red que mantenga el resolver esperando más de ese tiempo. | Aviso de tiempo agotado y sin otro resolver largo para probing. Cancelar disponible. Un error HTTP inmediato no prueba este caso; si no se logra la espera, NO EJECUTADO. |

Los límites no son un tiempo total exacto: arranque y limpieza cuestan tiempo.
Un enlace privado/borrado o una verificación no sirve para medir decodificación.
En Windows 10/11, la etiqueta `Windows 10.0` y motor oficial es correcta incluso
en Windows 11; comprobar allí es opcional y no aprueba XP.

## D. Medición en el Atom real

Usar primero **Atom · 240p**. Para comparar perfiles, conservar video accesible,
conexión y salida de audio; cerrar tareas pesadas. [SUPUESTO] No cambiar CPU,
driver ni Windows entre perfiles. Anotar el método de las mediciones aproximadas.

| Perfil | Primer cuadro (s) | CPU típica/máxima | RAM/VM/peak de TubeLiviano | RAM/peak total con yt-dlp/qjs | Cortes de imagen/audio | Estado |
| --- | --- | --- | --- | --- | --- | --- |
| 144p · Mínimo | | | | | | |
| 240p · Atom | | | | | | |
| 360p | | | | | | |

1. Anotar CPU/memoria antes de abrir y con Inicio en reposo 30 s; teclado y tamaño
   deben responder sin actividad continua de animación.
2. Reproducir los clips offline y luego el mismo video online al menos 96 s por
   perfil, si el servicio lo permite. Medir Reproducir→primer cuadro con cronómetro.
3. Anotar cuadros/cortes audibles y paginación intensa. Registrar procesos y picos
   de memoria; el EXE pequeño no representa RAM total de VLC y los motores.
4. Cancelar y esperar 10 s: no deben quedar motores y CPU debe volver al reposo.
   Repetir tres aperturas/cancelaciones y comparar memoria/handles si la herramienta
   los muestra; no inventar campos que XP no exponga.
5. Con segunda PC, repetir en Puente: H.264/24 fps y transcodificación en esa PC.
   Sin segunda PC: NO EJECUTADO.

Medir el ~14 % recibido al primer cuadro exige bytes del origen, no solo segundos
o la barra de VLC. Sin medición fiable, marcar **NO EJECUTADO**. El test Range del
build demuestra streaming temprano bajo sus condiciones, no un porcentaje fijo
en YouTube.

## Entrega del resultado

Conservar ficha, tabla Atom, `motor-xp.txt`, JSON y `resultado.txt` de `%TEMP%`.
Si falla, registrar pasos mínimos, perfil, versión detectada/motor, código exacto
y si fue offline u online. Siguiente mejor acción en hardware: **A1–A4 y
`--version`**, separando carga del frontend/VLC de carga del motor antes de medir.
