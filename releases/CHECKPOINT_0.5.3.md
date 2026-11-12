# Cierre de TubeLiviano 0.5.3

Código: `99a237e50e70781669b888d7556649ae23b77e5e`. Base publicada: `385d975` (0.5.2).

| Tarea | Estado | Evidencia / siguiente |
| --- | --- | --- |
| Botón fullscreen y teclas | Implementado | Win32/GDI; mismo reproductor, F11/Esc, pausa y restauración; pruebas físicas F1–F10 |
| Presets | Implementado | Cinco perfiles reales; Preparar/Guardar/Cancelar y preferencias ajenas preservadas; P1–P8 |
| Build x86 | Reproducible | Tres EXE idénticos de 89.088 bytes, PE32/i386, subsistema 5.01 |
| Pruebas actuales | Aprobadas por suite | 1296 comprobaciones sobre el mismo binario x64; 30 tests host/ZIP sin omitir |
| VLC real | Aprobado bajo Wine | Imagen visible antes/durante/después del fullscreen; clips 144/240/360 y Range |
| Documentación | Completa | README, notas, checkpoint y checklist 0.5.3 |
| ZIP | Reproducible | Dos empaquetados byte a byte iguales; datos debajo |
| GitHub | Archivos versionados en releases/ | Commit de estos artefactos y `git push origin HEAD:refs/heads/main` explícito |

La validación separa las suites aprobadas del runner y las repeticiones puntuales
del fixture de fullscreen: mismo SHA-256 de app `a8052f04f59efe581791817f3cd1d705dbb8a8677d1aeaf2e01454e05e65f3fa`.
No se declara que la primera corrida completa pasara: se corrigieron aislamiento
TEMP y espera de finalización del fixture, conservando las aserciones de restauración.
`docs/evidence/0.5.3/final-validation.json` y `fullscreen-handoff.json` identifican
comandos, resultados y alcance. El test real exige colores visibles del clip,
además del informe de imagen/audio de producción.

## Artefactos

- `TubeLiviano_Portable_0.5.3_x86.zip`: 53890497 bytes; SHA-256
  `d15a369f536a12b51a2c7bfacb300528d59ea7e5f77ebb38aab794324e11742c`.
- `TubeLiviano_ThirdParty_Sources_0.5.3.zip`: 45711135 bytes; SHA-256
  `7ae25dba7aa61d4576b230891b4bd94dc0dca112017d3ba1cdc19e6e0659d422`.
- EXE: SHA-256 `8c6e533fac019b13800e4fc65820b3a2ea93f51ad36aa471b6c69625304247f2`.

`package-report-0.5.3.json` y `host-tests-0.5.3.txt` son posteriores al empaquetado;
quedan fuera del ZIP para evitar una referencia circular al hash. Los ZIP anteriores
no se reemplazan. La publicación usa Git y no el botón Publicar del entorno.

Comandos ejecutados:

```sh
make -B PREFIX=/workspace/toolchain/root/usr/bin/i686-w64-mingw32-
python3 scripts/package.py --slim-vlc
python3 scripts/package.py --slim-vlc --output build/repeat-0.5.3
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

## Supuestos y límites

[SUPUESTO] Los presets cubren rendimiento y conexión, con Atom/240p como recomendado.

[NO EJECUTADO] XP/Atom físicos, RAM/fluidez y sonido audible. Los medios de Wine
usan audio dummy; las capturas corresponden a x64. El bookkeeping pre-maximizado
de Wine no se presenta como un escritorio XP real. YouTube en vivo está bloqueado
en este entorno; motores/framework y sus versiones no cambiaron. El ZIP supera
~40 MB y se mantienen los límites de fuentes transitivas de THIRD_PARTY.md.

Siguiente mejor acción: extraer 0.5.3 en carpeta nueva; usar el botón offline,
F11/Esc y los cinco presets. En XP ejecutar además `engines\xp\yt-dlp.exe --version`
y conservar el error exacto si falla. Checklist: `docs/PRUEBAS_0.5.3.md`.
