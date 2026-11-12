# Descargar TubeLiviano 0.5.3

**[Descargar TubeLiviano_Portable_0.5.3_x86.zip (53,89 MB)](https://github.com/kbdfh98uv/TubeLiviano/raw/refs/heads/main/releases/TubeLiviano_Portable_0.5.3_x86.zip)**

Extraé todo en una carpeta nueva y abrí `TubeLiviano.exe`; el título debe mostrar
**0.5.3**. Incluye VLC 3 x86, ambos motores separados, clips, prueba offline y
fuentes MIT. No mezclar DLLs anteriores ni ejecutar dentro del ZIP.

- Botón Pantalla completa en Resultados, F11 para alternar y Esc para volver
  conservando reproducción/pausa; barra inferior con Pausa, Cancelar y Volver.
- Configuración: cinco presets Mínimo, Atom, Conexión inestable, 360p y Puente.
  Elegir → Preparar → Guardar; Cancelar descarta el borrador.
- EXE x86: **89.088 bytes**; controles Win32/GDI y 240p predeterminado.

[Captura de fullscreen](../docs/images/0.5.3/fullscreen.png) ·
[Presets](../docs/images/0.5.3/settings-presets.png) ·
[Guía y pruebas físicas](../docs/PRUEBAS_0.5.3.md) ·
[Cierre/evidencia](CHECKPOINT_0.5.3.md)

Pruebas: 1296 comprobaciones combinadas sobre la misma app x64/Wine y 30 tests
host/ZIP aprobados. No certifican XP/Atom físicos: el framework XP y la fluidez
siguen pendientes de comprobar allí. Los medios automatizados usan audio dummy.

[Fuentes upstream de terceros (45,71 MB)](https://github.com/kbdfh98uv/TubeLiviano/raw/refs/heads/main/releases/TubeLiviano_ThirdParty_Sources_0.5.3.zip).
Las dependencias conservan sus licencias y límites de fuentes descritos en
[THIRD_PARTY.md](../THIRD_PARTY.md). Se mantienen todos los ZIP anteriores.

[Sumas SHA-256](SHA256SUMS.txt). Con GNU coreutils en esta carpeta:

```sh
sha256sum --check --ignore-missing SHA256SUMS.txt
```
