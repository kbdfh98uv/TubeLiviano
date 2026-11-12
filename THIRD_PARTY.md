# Componentes de terceros

La licencia MIT corresponde al código de TubeLiviano. **No cambia las licencias
de VLC, los motores yt-dlp, Python ni sus bibliotecas**. Los binarios VLC y yt-dlp
se distribuyen sin modificaciones; QuickJS se recompila como se explica abajo.
No hay instaladores ni descargas al iniciar.

| Componente incluido | Procedencia fijada | Licencia / notas |
| --- | --- | --- |
| LibVLC 3.0.21, x86 | Paquete oficial VideoLAN.LibVLC.Windows 3.0.21, NuGet | LGPL-2.1-or-later para libVLC; VLC y sus módulos/bibliotecas conservan sus licencias GPL/LGPL y otras. Carga dinámica, DLLs reemplazables. |
| yt-dlp x86 moderno | yt-dlp/yt-dlp, release 2026.10.26, `yt-dlp_x86.exe` oficial | Unlicense para yt-dlp; el ejecutable empaquetado incorpora dependencias con otras licencias, incluido GPLv3+. Ver `licenses/yt-dlp-THIRD_PARTY_LICENSES.txt`. Windows 10/11. |
| yt-dlp x86 para XP | nicolaasjan/yt-dlp, release 2026.10.29.091737, `yt-dlp_x86_winXP.exe` | Fork de terceros, **no es el build oficial**. Unlicense y licencias de dependencias. El autor lo publica explícitamente para Windows XP; requiere validación en XP real. |
| QuickJS-NG x86 | quickjs-ng/quickjs, v0.18.0; compilación propia para XP | MIT, ver `licenses/quickjs-LICENSE`. Intérprete JavaScript pequeño para los desafíos de YouTube, sin Node/Deno. |
| yt-dlp-ejs | 0.8.0, incluido dentro de los ejecutables upstream de yt-dlp | Unlicense AND MIT AND ISC; se incluye su código fuente en el archivo complementario. |
| Certifi / raíces Mozilla | certifi 2026.10.23, fuente oficial PyPI | MPL-2.0; se distribuye solamente `vlc/certs/cacert.pem` como datos. Avisos en `licenses/certifi-LICENSE` y texto en `licenses/MPL-2.0.txt`. No requiere Python adicional. |

El frontend es C nativo; los dos ejecutables upstream de yt-dlp sí contienen
intérpretes Python empaquetados. Se inicia solamente el correspondiente al OS y se
cierra al cancelar. No es posible afirmar que el paquete completo carece de Python
ni que su consumo de RAM será el del frontend. Se incluye QuickJS-NG en
`runtimes/qjs.exe` para resolver desafíos JavaScript mediante yt-dlp; esto tampoco
garantiza que YouTube permita todos los accesos o deje de exigir verificación.

## Reproducción de las dependencias

`python3 scripts/fetch-deps.py --sources` descarga los artefactos con TLS validado,
compara los motores y sus fuentes con los SHA-256 publicados por cada release y
comprueba el SHA-512 del paquete NuGet contra su catálogo oficial y el valor fijado
en el script. QuickJS se compila desde un tarball oficial con SHA-256 fijado. El
binario oficial QuickJS-NG importa APIs de Vista y no sirve para XP. Nuestra
compilación desactiva `os.Worker` y `Atomics`, no utilizados por el solucionador
JavaScript de yt-dlp; la modificación y comando de compilación están en
`scripts/fetch-deps.py:build_quickjs`. Se auditan imports para excluir APIs conocidas
posteriores a XP. Esto es evidencia estática, no una prueba en XP real.
Desde 0.5.2 el linker fija el timestamp PE en cero y la etapa temporal se valida
antes de reemplazar `qjs.exe`. Se exige PE32/i386, subsistema/OS 5.01 y únicamente
KERNEL32/MSVCRT; también se rechazan imports modernos conocidos. El manifiesto
registra compilador y flags. La reproducción offline verifica las mismas sumas
de entrada y falla si falta o está alterada la caché; no intenta descargar para
disimular un faltante. Dos builds con el mismo toolchain fueron idénticos en esta
sesión; no se garantiza identidad entre compiladores diferentes.
El ZIP no incorpora código .NET: de NuGet se extraen únicamente los
DLLs y recursos nativos x86. Los hashes y URLs quedan en
`licenses/dependencies.json`. No actualizar un motor con el de la otra carpeta.

Fuentes exactas de las versiones incluidas:

- VLC: <https://github.com/videolan/vlc/tree/3.0.21>;
  <https://codeload.github.com/videolan/vlc/tar.gz/refs/tags/3.0.21>.
  Incluye recetas, parches y sumas de dependencias en `contrib/src/` y las
  instrucciones de compilación Win32 en `extras/package/win32/`.
- Empaquetado nativo oficial: <https://code.videolan.org/videolan/libvlc-nuget>.
- yt-dlp oficial: <https://github.com/yt-dlp/yt-dlp/releases/tag/2026.10.26>.
- Fork XP: <https://github.com/nicolaasjan/yt-dlp/releases/tag/2026.10.29.091737>.
- QuickJS-NG: <https://github.com/quickjs-ng/quickjs/releases/tag/v0.18.0>;
  <https://codeload.github.com/quickjs-ng/quickjs/tar.gz/refs/tags/v0.18.0>.
- yt-dlp-ejs: <https://pypi.org/project/yt-dlp-ejs/0.8.0/>;
  <https://github.com/yt-dlp/ejs>.
- Certifi: <https://pypi.org/project/certifi/2026.10.23/>;
  su sdist fijado por SHA-256 de PyPI contiene el bundle Mozilla original y sus
  avisos y se incluye en el archivo de fuentes complementario.
- Licencias de bibliotecas Python y dependencias:
  `licenses/yt-dlp-THIRD_PARTY_LICENSES.txt`.

VLC recibe `--gnutls-dir-trust=<carpeta absoluta>/vlc/certs`, agregando estas
raíces actuales al almacén del sistema, sin desactivar validación de certificados
ni de nombres HTTPS. El script comprueba el SHA-256 del sdist y que el bundle PEM
se parsea completo. Esto evita depender exclusivamente del almacén obsoleto de XP;
no garantiza que toda combinación servidor/TLS/VLC/XP funcione sin prueba real.

`scripts/package.py` crea también `TubeLiviano_ThirdParty_Sources_0.5.3.zip` con los
archivos fuente upstream descargados, recetas y manifiesto. Al redistribuir, alojar
ese ZIP junto al portable, con el mismo acceso gratuito, y conservar estos avisos.
El archivo complementario contiene las fuentes VLC/yt-dlp y recetas contrib; no
contiene todos los tarballs de dependencias transitivas de VLC/Python. Antes de una
distribución pública debe completarse la provisión de las fuentes correspondientes
de esas dependencias según sus licencias. No se ofrece en nombre del usuario una
garantía u oferta escrita de suministro durante tres años.

## Tamaño y compatibilidad

Los motores descargados ocupan 18.62 MB (moderno) y 19.32 MB (XP) antes
de añadir VLC. Por tanto **~40 MB no es una promesa cumplida por este conjunto de
versiones**. El empaquetador informa el tamaño real y permite un perfil VLC reducido
explícito, que debe probarse antes de distribuir. No se sustituyen los ejecutables
por descargadores ni se omite uno para aparentar el tamaño solicitado.

La interfaz está dirigida a XP SP3 x86 y Windows 10/11. La declaración del autor
del fork, la arquitectura PE y una prueba bajo Wine no equivalen a certificación en
XP o en un Atom N270. Los cambios del servicio YouTube pueden inutilizar motores
antiguos aun cuando el programa y las DLLs se carguen correctamente.
