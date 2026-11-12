# Cierre de TubeLiviano 0.5.2

Código de la release: `76f91fc4fa80e3a4f4a398e588d21e9f31b669e0`. Base 0.5.1: `6b7357c`.
El ZIP se generó desde ese código; los registros de empaquetado quedan fuera de
su contenido para evitar que el hash dependa de sí mismo.

| Tarea | Estado | Evidencia | Siguiente |
| --- | --- | --- | --- |
| Releer 0.5.1 | Hecho | `docs/CHECKPOINT-0.5.2.md`, commit baseline `1d2a15d` | Cerrado |
| Build limpio | Hecho | Tres EXE idénticos, 84.992 bytes; `docs/evidence/0.5.2/release-build.json` | Probar x86 real |
| Auditoría y bugs | Hecho | A1–A9, puente sin perder foco y batch NTSTATUS con antes/después | XP/framework físico |
| Offline/Range | Aprobado | `release-validation.json`: tres perfiles y primer cuadro con 157.696/4.428.555 bytes (3,56 %) | Sonido/fluidez Atom |
| UI/configuración | Aprobado en x64/Wine | 120 checks UI, 180 settings, 302 controller; total suites 953, cero fallos | Revisar XP/Atom |
| Tests del paquete/host | Aprobado | 30 tests, cero omitidos; `host-tests-0.5.2.txt` | Cerrado |
| Documentación/checklist | Hecho | README, VALIDACION, RELEASE_NOTES y PRUEBAS_XP_ATOM | Ejecutar A1–A4 y motor --version |
| ZIPs reproducibles | Hecho | Dos empaquetados byte a byte idénticos; datos debajo | Verificar descarga |
| Publicación Git | Publicado en main | `41de10308c878a73559fae0066858390c1772bcd`, push explícito confirmado por `git ls-remote` | Cerrado |

## Archivos

- `TubeLiviano_Portable_0.5.2_x86.zip`: 53669582 bytes (53,67 MB),
  SHA-256 `4b73e0526426e65bb85a16dd63e95cbc5e5253ef26fc8ce8dd97ece48d43e892`.
- `TubeLiviano_ThirdParty_Sources_0.5.2.zip`: 45711134 bytes (45,71 MB),
  SHA-256 `89a87f1e1a8392f74ecf9ad1577df26bce063fa2d3699aa78c512ec9b15469c4`.
- EXE: SHA-256 `e02736d7087d3c8f228b1cf930b3214f5ee6fc8dfd3089e67858eeff2a8ec440`.

`package-report-0.5.2.json` describe los archivos incluidos/excluidos. Todas las
fuentes propias y evidencia anteriores al empaquetado están dentro del portable.
Los ZIP 0.5 y 0.5.1 no se reemplazan.

Comandos ejecutados, todos aprobados:

```sh
make -B PREFIX=/workspace/toolchain/root/usr/bin/i686-w64-mingw32-
# Runner completo: comando exacto en docs/VALIDACION.md y release-validation.json
python3 scripts/package.py --slim-vlc
python3 scripts/package.py --slim-vlc --output build/release-repeat
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

La presencia de este archivo y los ZIP en GitHub se publica mediante commit/push,
independientemente del botón Publicar del entorno. Comprobar el commit remoto:
`git ls-remote origin refs/heads/main` y `git log -2 --oneline`.

## Supuestos y riesgos abiertos

- [SUPUESTO] La referencia visual es el contenido oscuro de YouTube; se conserva
  Win32/GDI, sin navegador ni animaciones y 240p predeterminado.
- [NO EJECUTADO] XP SP3, Atom N270/512 MB y Windows físicos: no hay hardware.
  Wine x64 no ejecuta ni certifica el EXE x86. Sus contadores GDI/USER son cero,
  por lo que no se declara una medición de fugas o RAM física.
- [NO EJECUTADO] YouTube/TLS/desafíos reales: CONNECT bloqueado con HTTP 403 en
  el entorno de build; no se observó un HTTP 429 de YouTube.
- El framework XP mantiene sus binarios fijados; los avisos KERNELxp/ntext deben
  comprobarse en XP. No se afirma que el rediseño arregle ese framework.
- El ZIP supera la meta de ~40 MB. Fuentes transitivas pendientes de terceros y
  licencias se explican en THIRD_PARTY.md.

## Siguiente mejor acción al despertar

Extraer completo en carpeta nueva, confirmar 0.5.2 y motor XP; ejecutar A1–A4
(144/240/360p, sonido normal y batch) y `engines\xp\yt-dlp.exe --version`.
Guardar los JSON y el error exacto si existe. Después probar búsqueda, Cancelar,
Reintentar, preferencias, y medir 240p en el Atom según la ficha física.

## Confirmación de publicación

El push de código y artefactos terminó con salida cero; GitHub confirmó
`1d2a15d..41de103 HEAD -> main`. `git ls-remote origin refs/heads/main` devolvió
`41de10308c878a73559fae0066858390c1772bcd`. La advertencia por superar los 50 MB
recomendados no impidió el push.

La URL directa del portable respondió **HTTP 200** y `Content-Length: 53669582`,
igual al ZIP verificado. La comprobación HEAD confirma acceso y tamaño; el SHA-256
publicado permite comprobar la descarga completa. Evidencia en
`download-check-0.5.2.json`. Este cierre posterior no modifica los ZIP ni su código.
