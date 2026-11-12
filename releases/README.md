# Descargar TubeLiviano 0.5.2

**[Descargar TubeLiviano_Portable_0.5.2_x86.zip (53,67 MB)](https://github.com/kbdfh98uv/TubeLiviano/raw/refs/heads/main/releases/TubeLiviano_Portable_0.5.2_x86.zip)**

Extraé todo en una carpeta nueva y abrí `TubeLiviano.exe`; el título debe mostrar
**0.5.2**. No abras el EXE desde dentro del ZIP ni mezcles dependencias anteriores.

Nueva interfaz GDI: Inicio con buscador central, tema oscuro/claro, Resultados y
Configuración con calidad, búfer, hilos, audio, red y preferencias portables.
El frontend x86 ocupa **84.992 bytes**. Incluye VLC, motores separados XP/moderno,
QuickJS, tres clips offline, `Probar_offline.cmd` y código propio MIT.

Prueba automatizada: 953 comprobaciones Win32 en variante x64/Wine, clips
144/240/360p y Range, más 30 tests de host/ZIP. **[NO EJECUTADO] XP/Atom físicos**:
el framework XP aún requiere prueba; no se declara reparado sin ejecutarlo allí.

[Instrucciones](../README.md) · [Capturas](../docs/images/0.5.2/home-dark.png) ·
[Checklist XP/Atom](../docs/PRUEBAS_XP_ATOM.md) · [Cierre y evidencia](CHECKPOINT_0.5.2.md)

[Fuentes upstream de terceros (45,71 MB)](https://github.com/kbdfh98uv/TubeLiviano/raw/refs/heads/main/releases/TubeLiviano_ThirdParty_Sources_0.5.2.zip).
Las dependencias conservan sus licencias y las limitaciones de fuentes descritas
en [THIRD_PARTY.md](../THIRD_PARTY.md). Se conservan los ZIP 0.5 y 0.5.1.

[SHA-256 de los archivos](SHA256SUMS.txt). Desde esta carpeta con GNU coreutils:

```sh
sha256sum --check --ignore-missing SHA256SUMS.txt
```
