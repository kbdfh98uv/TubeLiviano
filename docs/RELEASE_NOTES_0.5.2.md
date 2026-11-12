# TubeLiviano 0.5.2 · notas de release

Base: `6b7357c` (0.5.1). Destino: portable Win32 x86, XP SP3/Atom y Windows
10/11. Los ZIP anteriores se conservan en `releases/`.

## Cambios desde 0.5.1

- La carga de libVLC usa la ruta privada de `vlc/` y búsqueda de dependencias
  relativa a ese DLL. Un `libvlccore.dll` sobrante en raíz no debe ganar al core
  correcto; no se modifica el directorio DLL global del llamador.
- Cerrar ignora Enter y no inicia otra extracción. Rutas offline largas fallan
  claramente. El self-test no aprueba tras error tardío de VLC ni cuando no puede
  escribir su informe.
- Se rechazan NUL incrustados y una carpeta nombrada `yt-dlp.exe`. El fallo se
  clasifica: timeout, lanzamiento/IO, DLL, crash y salida inválida no provocan
  otra resolución larga. La salida normal `1` conserva el probing de formatos.
- QuickJS se reconstruye con timestamp PE cero y valida arquitectura, subsistema
  XP e imports. Fuentes/hashes upstream siguen fijados; dos builds del mismo
  toolchain resultaron idénticos.
- El empaquetador verifica recursos, hashes, motores, VLC y tres clips antes de
  reemplazar ZIPs mediante archivos temporales. Excluye e informa archivos ajenos;
  no incorpora un informe viejo ni DLL accidental de la etapa.
- Un runner offline y **`Probar_offline.cmd`** hacen verificable imagen/audio en
  Windows sin Python/PowerShell, con informes nuevos por ejecución. Audio dummy
  se distingue de sonido audible real.

## Interfaz y configuración

La UI solicitada conserva controles Win32/dibujo GDI: oscuro por defecto, claro
opcional, Inicio centrado, Resultados con título/canal/duración, navegación
inferior y Configuración. No añade navegador embebido, imágenes remotas ni
animaciones. Texto grande y cliente mínimo 660×450 mantienen las acciones en
pantallas pequeñas.

Configuración reúne calidad, búfer, prefetch, hilos, volumen/silencio, 6/12
resultados, timeouts, modo/dirección del puente y memoria del tamaño. Atom/Mínimo
cambian solo calidad/búfer/prefetch/hilos. El INI es portable; un fallo de guardado
aplica la sesión sin destruir el archivo previo. El token no se persiste. Inicio
detiene/cancela y conserva contexto, sin abrir un video antiguo al cambiar calidad.
La dirección del puente recién escrita se sincroniza al abrir Configuración o
cerrar, incluso sin perder el foco; se agregaron regresiones antes/después.
El batch exige código de salida exactamente cero, incluidos los fallos negativos
NTSTATUS, además de un JSON aprobado.

## Evidencia y alcance

La [auditoría](AUDITORIA-0.5.2.md) identifica A1–A9 y el
[checkpoint](CHECKPOINT-0.5.2.md) registra comandos y cierre. Los informes están
en `docs/evidence/0.5.2/`, con arquitectura y límites explícitos.

- Base 0.5.1 desde dos fuentes limpias: EXE idénticos de 54.784 bytes,
  SHA-256 `3146961306d4aafb6a35c6a67534cbf6d7e8abf80ebf86ebb613de57ff105790`.
- QuickJS reproducido dos veces: timestamp cero y hashes iguales. No se afirma
  que cambiar compilador o FFmpeg produzca los mismos bytes.
- Regresiones del hito de estabilidad en variante temporal x64/Wine: 108 checks
  de motor, 35 de errores del frontend, 23 de OS y 19 de carga de VLC aprobados.
  Los casos NUL/directorio eran aceptados por la base y se rechazan tras el fix.
- Ese hito reprodujo 144/240/360p y `Range: bytes=0-` desde un MP4 local de 96 s,
  con contadores de video/audio positivos. Usa audio dummy y origen controlado.
- Candidata final integrada: EXE x86 de 84,992 bytes, idéntico en
  tres builds. 953 comprobaciones Win32 en la variante x64/Wine, cero fallos;
  incluye 120 de UI/configuración y 127 del batch con códigos negativos.
- Los tres perfiles reales pasaron imagen/audio. Range mostró primer cuadro con
  157,696 de 4,428,555 bytes (3.56%).
  La ventana de observación se amplió a 12 s para incluir arranque frío, sin quitar
  las exigencias de cuadros/audio y descarga parcial. [Validación detallada](VALIDACION.md).
- Tamaño y SHA-256 de los ZIP y controles posteriores al empaquetado:
  [cierre de publicación](https://github.com/kbdfh98uv/TubeLiviano/blob/main/releases/CHECKPOINT_0.5.2.md).

## Riesgos abiertos

- **[NO EJECUTADO] XP SP3, Atom N270/512 MB y Windows físicos:** sin hardware en
  este entorno. Se entrega [checklist exacto](PRUEBAS_XP_ATOM.md).
- **[NO EJECUTADO] YouTube y desafíos EJS reales:** el acceso al servicio está
  bloqueado en el entorno. 403/429 o verificación pueden impedir reproducción.
- Los motores mantienen sus binarios verificados; 0.5.2 no prueba el framework
  XP del equipo que mostró errores. No se instalan extensiones de Windows ni se
  cambian DLLs del sistema para ocultar ese límite.
- La meta de ~40 MB no se promete; consultar tamaño/sumas definitivos. El EXE
  pequeño no representa el consumo total de VLC y los intérpretes de yt-dlp.
- Código propio MIT; dependencias con sus licencias. El compañero no contiene
  todos los tarballs transitivos VLC/Python: completar su provisión para
  redistribución pública sigue pendiente en [THIRD_PARTY.md](../THIRD_PARTY.md).

## Archivos y verificación

`TubeLiviano_Portable_0.5.2_x86.zip`: EXE, dependencias, clips, batch, fuentes
propias y guías. `TubeLiviano_ThirdParty_Sources_0.5.2.zip`: fuentes upstream y
recetas. Hashes publicados: `releases/SHA256SUMS.txt`.

```sh
cd releases
sha256sum --check --ignore-missing SHA256SUMS.txt
```

El comando verifica los ZIP presentes sin exigir que también se descarguen las
versiones anteriores; debe verificar al menos un archivo.

En XP sin SHA-256 disponible, no se exige PowerShell ni se inventa una verificación:
usar la ficha y el batch offline. Siguiente mejor acción física: cargar frontend,
motor y tres clips antes de intentar YouTube y medir Atom.
