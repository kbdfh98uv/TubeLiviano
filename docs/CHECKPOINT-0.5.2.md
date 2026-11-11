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
| 5. Bugs de selección/DLL | En curso | Correcciones A1–A9 del informe de auditoría, con regresiones asociadas |
| 6. Pruebas automatizadas | Pendiente | Cubrir los bugs y límites encontrados |
| 7. Documentación/release notes | Pendiente | Actualizar con los resultados reales |
| 8. Checklist XP/Atom | Pendiente | Pasos exactos y resultados esperados |
| 9. Release 0.5.2 | Pendiente | Solo después de cerrar los pasos posibles sin hardware |

## Decisiones y criterio de parada

No se repite el diagnóstico de IA32/Wine ya documentado ni se sustituyen motores
por builds sin sumas verificables. Cada lote dejará un comando, informe, test o
corrección verificable. Dos lotes sin mejora medible cierran esa rama; se explicará
el motivo. La próxima mejor acción es terminar las correcciones A1–A9 y ejecutar
sus regresiones, antes de revisar documentación y preparar la versión.

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
