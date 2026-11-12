# Auditoría desde 0.5.1

Base inspeccionada: `6b7357c`. Los hallazgos se registraron antes de modificar
el producto, siguiendo el orden del backlog. Evidencia estática en
`evidence/0.5.2/compatibility-audit.json`.

## Compatibilidad y build

- `app.manifest`: XML válido, `asInvoker`, sin dependencia de Common Controls
  v6 ni ensamblados externos; el GUID Win10 solo afecta el reconocimiento del OS.
- `app.rc`: manifiesto y versión presentes. La versión está duplicada en varios
  lugares: se debe comprobar su coherencia al empaquetar.
- `Makefile`: i686, MSVCRT, subsistema/OS 5.01, `-static-libgcc`, sin timestamps
  del linker. Dos builds desde fuentes limpias produjeron el mismo EXE de
  54.784 bytes y el mismo SHA-256 que el entregable 0.5.1.
- El frontend importa GDI32, KERNEL32, MSVCRT, SHELL32 y USER32. VERSION.dll y
  APIs modernas se buscan dinámicamente. No se agregan imports post-XP.
- Ambos motores son PE32/i386 y coinciden con sus hashes fijados. Se mantienen
  separados; no hay motivo verificable para sustituirlos durante esta sesión.
- Las rutas de ejecución parten de la ubicación del EXE. `/workspace` aparece
  solo como fallback del toolchain del host de build, no en rutas de ejecución
  del producto. La IP del puente es un valor inicial editable.
- [NO EJECUTADO] Carga real del frontend/motores en XP o Atom: no hay hardware.

## Hallazgos a corregir y comprobar

| ID | Hallazgo concreto | Evidencia | Comprobación prevista |
| --- | --- | --- | --- |
| A1 | Una DLL core antigua en la raíz puede ganar a `vlc/libvlccore.dll` | `player_init`: `SetDllDirectory` conserva la raíz del EXE antes de la carpeta VLC al resolver dependencias | Señuelo incompatible en raíz; el reproductor debe cargar el core de `vlc/` |
| A2 | Enter puede iniciar otra extracción durante el cierre | Atajo en `WinMain` llama directamente a `search_query`, fuera del guard de `WM_COMMAND` | Cierre demorado + Enter no crea otro proceso |
| A3 | Ruta offline larga puede quedar sin NUL | `_snwprintf` del clip se usa antes del guard de longitud del reproductor | Carpeta larga válida para el EXE da error claro y no se bloquea |
| A4 | Self-test puede aprobar después de un fallo de VLC o sin informe escrito | El resultado usa contadores previos y no exige estado distinto de 7; `report_test` ignora errores de archivo | Error después de cuadros positivos y destino JSON inválido deben fallar |
| A5 | Respuesta con NUL interno se acepta truncada | `run` conoce `outlen`, pero entrega solo una cadena a los parsers | JSON/URL seguida por NUL + basura debe rechazarse |
| A6 | Una carpeta llamada `yt-dlp.exe` se acepta como motor instalado | `engine_init` solo comprueba `INVALID_FILE_ATTRIBUTES` | Directorio con ese nombre debe rechazarse al iniciar |
| A7 | Timeout/fallo de lanzamiento puede provocar otro resolver de 90 s | El fallback de formatos solo distingue `loader_failed` | Fallos de infraestructura no deben repetir el probing |
| A8 | QuickJS no se reconstruye de forma determinista | Su comando carece de `--no-insert-timestamp`; el PE actual tiene timestamp `1794350466`, mientras el frontend tiene 0 | Dos builds QuickJS idénticos; validar también máquina PE32/subsistema/imports |
| A9 | El empaquetador admite archivos accidentales y no exige clips | Recorre toda la etapa y su lista obligatoria omite los tres MP4 | Rechazar recursos obligatorios faltantes y excluir archivos ajenos al paquete |

## Ramas descartadas

- No se afirma truncamiento por el presupuesto de lectura final de 64 KiB:
  el tamaño del pipe y la escritura bloqueante no dieron un caso reproducible.
  Se evita cambiar esa lógica por especulación.
- No se repite la investigación previa de IA32/QEMU ni se atribuye a XP una
  ejecución x64/Wine. Tampoco se buscan motores antiguos sin sumas verificables.
- La revisión inicial no incluyó cambios visuales. La nueva instrucción del
  usuario amplió el alcance a una interfaz GDI y configuración nativa; se conserva
  el presupuesto de recursos y los perfiles 144/240/360p.

Los hallazgos A1–A9 se corrigieron con regresiones reproducibles. La base 0.5.1
y la primera candidata 0.5.2 pasaron reproducción offline; la interfaz y las
preferencias nuevas requieren la validación final registrada en el checkpoint.
