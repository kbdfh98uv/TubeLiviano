# Checkpoint de la sesión 0.5.2

Inicio: 2026-11-11. Base: `6b7357c` (0.5.1), rama local `work`, destino remoto
`main`. Árbol de trabajo limpio al comenzar. La rama local es la del entorno;
se usará `git push origin HEAD:refs/heads/main` explícitamente.

## 1. Estado releído

- `6b7357c`: detección del kernel real y APIs del OS, manifiesto Win10,
  aislamiento de variables heredadas de PyInstaller/compatibilidad y errores DLL
  sin repetir el probing. Se agregaron pruebas y el portable 0.5.1.
- `dda26fd`: publicación de los ZIP 0.5 bajo `releases/` mediante Git.
- `5a5d5dc`: cliente Win32, libVLC, motores, puente, pruebas y documentación.
- Se conservaron los archivos publicados 0.5 y 0.5.1 y sus sumas SHA-256.
- Herramientas existentes: MinGW-w64 GCC 14/MSVCRT x86, Wine 10 x64 y Xvfb.
- [SUPUESTO] Se mantiene XP SP3 x86 como mínimo y 240p como predeterminado.
- [NO EJECUTADO] XP/Atom físicos: no hay hardware disponible. No se contarán
  las pruebas x64/Wine como ejecución del entregable x86.

## Avance ordenado

| Paso | Estado | Evidencia / siguiente |
| --- | --- | --- |
| 1. Releer estado | Hecho | `git status --short`, `git log -5 --oneline`, `git show --stat 6b7357c` |
| 2. Build limpio | Hecho | Dos `make -B PREFIX=/workspace/toolchain/root/usr/bin/i686-w64-mingw32-`, salida 0; EXE idénticos de 54.784 bytes, SHA-256 `3146961306d4aafb6a35c6a67534cbf6d7e8abf80ebf86ebb613de57ff105790`; `evidence/0.5.2/clean-build.json` |
| 3. Auditoría de recursos/build/motores/VLC | Hecho | `AUDITORIA-0.5.2.md`, hashes/imports en `evidence/0.5.2/compatibility-audit.json` |
| 4. Prueba offline reproducible | Hecho | `scripts/verify-offline.py`, baseline 144/240/360p y Range aprobados; `evidence/0.5.2/offline-baseline.json` |
| 5. Bugs de selección/DLL | Hecho | A1–A9 corregidos; regresiones antes/después en `evidence/0.5.2/` |
| 6. Pruebas automatizadas | Hecho | Runner final: 953 comprobaciones Win32 sin fallos, tres perfiles reales y Range; `release-validation.json` |
| 7. Documentación/release notes | Hecho | README, VALIDACION y RELEASE_NOTES_0.5.2 actualizados con alcance real |
| 8. Checklist XP/Atom | Hecho | `PRUEBAS_XP_ATOM.md`: pasos, resultados esperados y ficha física |
| 9. Release 0.5.2 | Registro externo | ZIP, checksums, pruebas del archivo y commit/push se cierran en `releases/CHECKPOINT_0.5.2.md` |

## Decisiones y criterio de parada

No se repite el diagnóstico de IA32/Wine ya documentado ni se sustituyen motores
por builds sin sumas verificables. Cada lote dejará un comando, informe, test o
corrección verificable. Dos lotes sin mejora medible cierran esa rama; se explicará
el motivo. La siguiente mejor acción en hardware es A1–A4 del checklist físico y
`engines\xp\yt-dlp.exe --version`: primero separar frontend/VLC de framework XP,
luego medir rendimiento y servicio online.

## Lote de referencia cerrado

Los pasos 1–4 mejoraron trazabilidad sin modificar el producto: reproducibilidad
demostrada, auditoría de imports/hashes y un runner offline que no confunde x64
con x86. No se repitieron intentos IA32/QEMU ni se usaron resultados antiguos como
si fueran de esta sesión.

Comando baseline ejecutado (salida 0, dos métodos de prueba, tres perfiles):

```sh
WINEDLLOVERRIDES='mscoree,mshtml=' python3 scripts/verify-offline.py \
  --surrogate-x64 \
  --cc-prefix /workspace/toolchain/root/usr/bin/x86_64-w64-mingw32- \
  --wine /workspace/toolchain/wine-10.0-amd64-wow64/bin/wine \
  --wine-prefix /workspace/toolchain/wineprefix10 \
  --xvfb /workspace/toolchain/root/usr/bin/Xvfb \
  --report-dir build/reports/session-0.5.2/offline-baseline
```

Primer cuadro de la prueba Range: 145.408 de 4.428.555 bytes (3,28 %), petición
`bytes=0-`. [NO EJECUTADO] Audio audible físico: el sink automático es dummy.

## Ampliación de alcance: interfaz y configuración

El usuario pidió enfocar el trabajo en la interfaz, tomando como referencia el
contenido oscuro de YouTube. [SUPUESTO] No se replica el navegador de la captura.
Se implementa dibujo GDI, controles Win32, búsqueda central, navegación Inicio/
Resultados y configuración portable; se mantienen motores, perfiles y streaming.

Primera compilación integrada: `make -B PREFIX=/workspace/toolchain/root/usr/bin/i686-w64-mingw32-`,
EXE x86 de 83.968 bytes, sin avisos con `-Werror`. Se prueba la integración antes
de empaquetar; esta cifra preliminar no sustituye el informe final.

Los pasos 5 y 6 de la auditoría inicial se cerraron antes de ampliar la UI:
regresiones reales de cargador VLC (19), app (35), motor (108 en ese punto),
OS (23) e interfaz anterior (32), más reproducción 144/240/360p y Range. Los
resultados de `offline-final.json` corresponden a esa candidata **anterior a la
interfaz nueva**. Se conservarán como trazabilidad, y la validación del diseño
nuevo se registrará por separado.

## Cierre de código y validación

La integración final conserva los tres perfiles y motores separados. Se corrigió
la altura integral del listbox, Escape dentro de un selector abierto y el guardado
de la URL del puente sin pérdida de foco. El batch exige salida exactamente cero,
incluso si un proceso escribió un JSON aprobado antes de terminar con NTSTATUS
negativo. Hay regresiones antes/después para URL y códigos negativos.

`release-build.json` registra EXE de 84.992 bytes y SHA-256
`e02736d7087d3c8f228b1cf930b3214f5ee6fc8dfd3089e67858eeff2a8ec440`, con fuentes verificadas e idéntico
en tres builds. `release-validation.json` registra la última ejecución integrada
con 953 checks aprobados, reproducción real 144/240/360p y Range.

[SUPUESTO] La referencia visual es el contenido de YouTube y no el navegador.
Se mantiene GDI/control Win32, sin animaciones ni imágenes remotas. No se midieron
RAM, CPU ni recursos GDI/USER físicos; Wine devuelve cero para esos contadores.

La publicación final se registra fuera del contenido del ZIP para que su hash no
dependa de sí mismo: [cierre en GitHub](https://github.com/kbdfh98uv/TubeLiviano/blob/main/releases/CHECKPOINT_0.5.2.md).
La rama se termina tras validar y publicar, sin ampliar alcance por relleno.
