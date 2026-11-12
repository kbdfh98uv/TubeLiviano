# Pruebas manuales 0.5.4 · espera hasta reproducir

**[NO EJECUTADO] XP/Atom físicos en el entorno de build.** Esta guía sirve para
medir el equipo real. Usar APROBADO, FALLO, BLOQUEADO o NO EJECUTADO; no contar
«Reproduciendo» como prueba de imagen o sonido. Anotar el error literal y separar
problemas del motor, red y salida de VLC.

## Preparación comparable

1. Extraer todo el portable en una carpeta nueva, por ejemplo `C:\TL054`, y abrir
   el EXE dentro de TubeLiviano. Confirmar título 0.5.4 y motor correspondiente al
   Windows detectado. No mezclar DLLs/índices de otra arquitectura o versión.
2. Anotar `winver`, Service Pack, CPU/RAM, resolución, disco/pendrive y antivirus.
   En Configuración preparar Atom y Guardar: Directo, 240p, 450 ms, 128 KiB, un hilo.
3. Cerrar tareas pesadas y usar el mismo video público, calidad y conexión para
   comparar. Conservar exactamente el mismo enlace o resultado seleccionado.
   Si se compara 0.5.3, extraerlo en otra carpeta y alternar versiones; no copiar
   los índices de plugins entre ellas.
4. Abrir Administrador de tareas (Ctrl+Mayús+Esc). Mostrar nombres/PID y memoria;
   elegir actualización rápida si está disponible. Registrar columnas y método:
   sumar working sets es solo una aproximación, no una medición exacta de RAM privada.

Medir con cronómetro **Buscar → resultados**, **Reproducir → primer cuadro visible**
y **Reproducir → primer sonido audible** por separado. Si no se puede ver el instante
con precisión, marcarlo como aproximado o NO EJECUTADO. Repetir tres veces cada
escenario principal y conservar las tres cifras, no solo la mejor. No cambiar
calidad o red a mitad de una comparación.

## Imagen/audio y reapertura

| ID | Pasos exactos | Resultado esperado |
| --- | --- | --- |
| L1 | Sin red, pulsar Probar imagen y audio con 240p desde una carpeta recién extraída. Cronometrar primer cuadro/tono; repetir con 144p y 360p. | Imagen animada y tono en cada perfil; anotar tiempos reales. La primera preparación puede crear `vlc\plugins\plugins.dat`; no se promete eliminar ese trabajo inicial. |
| L2 | Cerrar normalmente y abrir la misma carpeta; repetir el clip de 240p. | Reproduce correctamente con el índice existente. Registrar si mejora; no declarar APROBADO solo porque exista el archivo. |
| L3 | Ejecutar `Probar_offline.cmd` y conservar sus JSON. | Tres perfiles aprobados y código cero; audio dummy. L1 comprueba los parlantes y latencia audible real. |
| L4 | Con red, buscar un título conocido. Elegir un resultado y pulsar Reproducir. Anotar búsqueda y arranque por separado. | Resultados utilizables, luego imagen y audio. 403/429/verificación se registra como BLOQUEADO online, no como reproducción aprobada. |
| L5 | Tras al menos dos segundos de imagen/audio, Cancelar y abrir otra vez el mismo resultado a 240p antes de dos minutos. | Puede reutilizar su enlace reciente y evitar otra resolución; sigue haciendo streaming. Registrar tiempo y procesos. No esperar apertura instantánea garantizada. |
| L6 | Tras L5, pulsar Reintentar conservando el enlace y la calidad. | Se solicita una resolución nueva, aunque la combinación estuviera en memoria; no se obliga a pegar de nuevo. Registrar el tiempo aparte de L5. |
| L7 | Reproducir 144p hasta obtener imagen/audio; volver a 240p dentro de dos minutos, con el mismo enlace. | Cada calidad no conocida puede resolver una vez. Volver a una combinación vigente puede usar su entrada; calidad y enlace no se confunden. Solo caben dos entradas. |
| L8 | Cancelar una reproducción validada, esperar al menos 2 min 10 s y volver al mismo enlace/calidad. Repetir después de cerrar/reabrir la app. | La entrada vencida o perdida al cerrar debe resolverse de nuevo. Reabrirla no prolonga indefinidamente su vencimiento. |
| L9 | Si se dispone de una condición controlada de URL guardada caducada/error o red interrumpida, abrir esa entrada. | Un fallo o falta de imagen durante unos seis segundos después de preparar VLC provoca resolución fresca. Si sigue fallando, aparece un error/reintento y no un bucle interminable. Sin condición reproducible: NO EJECUTADO. |
| L10 | Iniciar un enlace que resuelva lentamente y pulsar Cancelar. Repetir cerrando la ventana durante la resolución. | La ventana responde y terminan el motor y sus descendientes de esa instancia. El inicio de VLC no debe dejar la interfaz bloqueada. Registrar tiempo de liberación. |
| L11 | Repetir L4–L7 tras usar F11/Esc y Pausa; mantener los mismos perfiles. | La optimización no reinicia el video al alternar fullscreen ni pierde pausa, enlace o calidad. Los controles anteriores siguen funcionando. |

La caché de enlaces guarda solo URLs en memoria, por combinación de enlace/calidad,
y solo tras una apertura con imagen/audio. El índice de plugins es otro archivo;
que persista al cerrar no significa que persistan videos o URLs. La memoria de
VLC puede permanecer en `TubeLiviano.exe` para otra apertura; no confundirla con
un proceso Python retenido.

## Índice portable y carpetas protegidas

Usar una **copia de prueba** de la carpeta, con TubeLiviano cerrado para alterar
archivos. El índice se genera para el VLC/arquitectura presentes; no copiar el
índice x64 de las pruebas al paquete x86.

| ID | Pasos exactos | Resultado esperado |
| --- | --- | --- |
| I1 | En la copia, borrar solo `vlc\plugins\plugins.dat` si existe. Abrir y reproducir el clip local; cerrar normalmente. | Reproduce y puede regenerar el índice si la carpeta permite escribir. No aparece un diálogo repetido de DLL ni exige reinstalar Windows. |
| I2 | Con índice existente, ejecutar `attrib +R vlc\plugins\plugins.dat`; abrir/reproducir y cerrar. Luego quitar el atributo. | El índice de solo lectura sigue permitiendo reproducción; el archivo no necesita sobrescribirse en cada apertura. |
| I3 | Probar una copia sin índice en un medio realmente protegido contra escritura o una carpeta de prueba sin permiso de crear archivos. | Reproduce con escaneo normal aunque no pueda escribir el índice. Registrar aviso/error si lo hubiera. No basta marcar una carpeta como Solo lectura en Explorador: eso no prueba una denegación de escritura en Windows. Sin medio/permiso adecuado: NO EJECUTADO. |
| I4 | Mover la carpeta completa, con su índice, a otra ruta o pendrive y abrir desde un directorio de trabajo distinto. | Encuentra sus plugins por ruta portable y mantiene la reproducción; si el índice no sirve, no debe quedar inutilizado el reproductor. |

Comandos de I2, desde la carpeta del EXE y con la app cerrada:

```bat
attrib +R vlc\plugins\plugins.dat
rem Ejecutar la prueba y cerrar antes de quitar el atributo.
attrib -R vlc\plugins\plugins.dat
```

## Procesos de yt-dlp y memoria

El worker pasó pruebas offline con el actor C, el plugin y un motor oficial x64
bajo Wine. Esta sección comprueba su comportamiento en el equipo real: esas
pruebas no validan el framework XP ni la fluidez del Atom.

Observar `yt-dlp.exe`, `qjs.exe`, `python.exe` y sus descendientes/PID: el intérprete
empaquetado puede aparecer como otro `yt-dlp.exe`, no con el nombre Python. LibVLC
está dentro de `TubeLiviano.exe`; no se exige un proceso `vlc.exe` separado.

| ID | Pasos exactos | Resultado esperado de aceptación |
| --- | --- | --- |
| W1 | Buscar y, en menos de 12 s desde los resultados, reproducir un seleccionado. Anotar PID, picos y tiempos. | Puede reutilizar el intérprete del motor entre búsqueda y resolución, evitando otro arranque. No inferir un ahorro fijo a partir de que se repita un PID. |
| W2 | Buscar, dejar la lista sin actividad 20 s y observar. Después elegir un video. | Tras unos 12 s de inactividad termina el motor conservado. La siguiente reproducción puede necesitar un arranque nuevo; no queda Python residente indefinidamente. |
| W3 | Resolver y dejar un video reproduciendo al menos 10 s. Revisar procesos durante ese período. | No queda el intérprete de yt-dlp/Python ni QuickJS de esta instancia retenido durante la reproducción. Puede existir una transición breve de limpieza tras la respuesta; medirla, sin llamarla residencia permanente. |
| W4 | Cancelar una búsqueda/resolución lenta; repetir cerrando la app. | Termina todo el árbol del motor, incluido el intérprete y un runtime JS que hubiera iniciado. No matar manualmente los procesos para aprobar la prueba. |
| W5 | En XP ejecutar `engines\xp\yt-dlp.exe --version`; en Win10/11 comprobar el motor oficial. Repetir W1–W4 cuando el framework cargue correctamente. | Versión/código cero y uso del motor correcto. Error KERNELxp/ntext es FALLO de carga; conservar texto y no reemplazar el motor XP por el moderno para ocultarlo. |

El frontend pequeño no representa la memoria total. Registrar pico conjunto y
paginación con 512 MB; una mejora de milisegundos que provoque paginación intensa
puede empeorar la experiencia del Atom. No se midió ese hardware en el build.

## Ficha mínima para devolver resultados

```text
Equipo / Windows y SP / CPU y RAM / resolución:
Disco o pendrive / antivirus / versión 0.5.4 / motor mostrado:
Video/enlace exacto y calidad / preset / estado del índice:
Escenario / repetición 1, 2, 3 / mediana aproximada:
Buscar→resultados (s):
Reproducir→primer cuadro visible (s):
Reproducir→primer sonido audible (s):
RAM/CPU iniciales, máximas y al terminar / columnas usadas:
PIDs antes, durante búsqueda, durante video y tras 20 s idle:
Resultado APROBADO/FALLO/BLOQUEADO/NO EJECUTADO:
Error literal / JSON / capturas:
```

Siguiente mejor acción: L1–L7 e I1–I3 en XP/Atom real; después W1–W5 para comprobar la reutilización y liberación del motor. Las regresiones anteriores de interfaz/presets
siguen en [PRUEBAS_0.5.3.md](PRUEBAS_0.5.3.md), usando 0.5.4 como título esperado.
