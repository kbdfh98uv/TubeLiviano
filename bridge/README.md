# Puente opcional · TubeLiviano 0.5

Este componente se ejecuta **en otra PC potente** de la red. La netbook continúa
ejecutando únicamente TubeLiviano/VLC nativos; no necesita Python ni FFmpeg.
El modo directo no requiere este servicio.

## Preparar la PC puente

Se necesita Python 3.9+ (recomendado 3.12+), FFmpeg con `libx264`/AAC y un yt-dlp
actual. Instale los motores desde sus proyectos oficiales y compruebe que los
comandos `python --version`, `ffmpeg -version` y `yt-dlp --version` funcionan.
En Windows también puede indicar rutas a sus ejecutables. Puede reutilizar el
yt-dlp oficial de 32 bits y QuickJS incluidos en este ZIP; no hace falta instalar
otro yt-dlp en la PC puente. Desde la raíz del paquete extraído, por ejemplo:

```console
python source/bridge/server.py --host 192.168.1.20 --yt-dlp engines/modern/yt-dlp.exe --js-runtime quickjs:C:\TubeLiviano\runtimes\qjs.exe --ffmpeg C:\herramientas\ffmpeg.exe
```

Sustituya la ruta absoluta de `qjs.exe` por la ubicación real; si contiene
espacios, encierre todo `quickjs:C:\...\qjs.exe` entre comillas. `--js-runtime`
se entrega a yt-dlp como `--js-runtimes`; al omitirlo se conserva la selección
predeterminada de yt-dlp (normalmente Deno). Python/FFmpeg se necesitan solo en
la PC puente. FFmpeg no está incluido en el cliente portable.

En Linux, desde la raíz del repositorio con yt-dlp/FFmpeg instalados:

```sh
python3 bridge/server.py --host 192.168.1.20
```

Sustituya `192.168.1.20` por la IP real de la PC puente. Sin `--host` solo se
admiten conexiones de la propia PC (`127.0.0.1`). Permita TCP 8787 en el firewall
**solo para su LAN**. No abra ni redireccione este puerto a Internet. En una LAN
compartida agregue `--token una-clave-local` y configure el mismo token en la URL
base usada por el cliente: `http://192.168.1.20:8787?token=una-clave-local`.
El campo del cliente admite tokens de letras ASCII, números, `-`, `_`, `.` y
`~`, sin espacios. HTTP en LAN no cifra el token ni el contenido.

En TubeLiviano elija **Puente**, introduzca la dirección de la PC puente y
reproduzca el enlace. La URL entregada a VLC tiene este contrato:

```text
http://192.168.1.20:8787/play?url=https%3A%2F%2Fyoutu.be%2FID&height=240
```

El token opcional se envía como `&token=...` o cabecera `Authorization: Bearer ...`.
`GET /health` informa versión, perfiles y si el único puesto de reproducción está
ocupado. Con token configurado, `/health` también lo exige.

## Qué hace

- yt-dlp resuelve el enlace en la PC potente. Solo se admiten URLs de YouTube;
  no sirve para consultar archivos o servicios arbitrarios de su red.
- FFmpeg produce H.264 baseline, YUV420p a 24 fps, AAC estéreo de 64 kbit/s,
  dentro de MPEG-TS. El perfil fija un máximo de 144/240/360 líneas; nunca sube
  la resolución de una fuente más pequeña. 240p es el predeterminado.
- Se transmite en cuanto llegan los primeros paquetes. Se conservan como máximo
  cuatro bloques de 16 KiB entre FFmpeg y el socket; no se descarga ni almacena el
  video completo. FFmpeg lee a tiempo real para evitar preparar por adelantado
  todo el video en la PC puente.
- MPEG-TS en vivo termina por cierre HTTP y no ofrece búsqueda por `Range`.
  La reproducción directa usa las URLs HTTP originales y sus capacidades Range.
- Un único video simultáneo; otro cliente recibe HTTP 503. Al cancelar en VLC o
  desconectar la red se termina FFmpeg y su grupo de procesos; también se cancela
  yt-dlp si el cliente desaparece durante la resolución. Resolución limitada a
  50 s, espera sin datos de 30 s y sesiones de 4 horas como máximo.

YouTube puede exigir cookies/verificación, un runtime JavaScript moderno o
devolver HTTP 429. Actualice y pruebe yt-dlp en esta PC. El puente ignora archivos
de configuración globales de yt-dlp para que el comportamiento sea repetible;
no transfiere cookies de navegadores ni incluye credenciales.

## Prueba offline

Desde la raíz del paquete extraído:

```console
python source/bridge/server.py --test-file media/prueba-240.mp4
```

Desde la raíz del repositorio, después de generar los clips:

```sh
python3 bridge/server.py --test-file build/portable/media/prueba-240.mp4
```

Con `--test-file`, una URL YouTube sintácticamente válida reproduce ese archivo
en lugar de resolver Internet. Es un modo de prueba explícito; no permite elegir
archivos arbitrarios desde la petición HTTP. Para ejecutar la validación real
de los tres perfiles, arranque con FFmpeg/ffprobe en PATH:

```sh
python3 -m unittest discover -s tests -p test_bridge.py -v
```

El comando anterior parte de la raíz del repositorio. En el ZIP, ejecute primero
`cd source` para usar las mismas rutas de pruebas.

Las pruebas generan imagen y tono locales, verifican H.264/AAC/24 fps a
144/240/360p, salida temprana de una fuente de 96 s, exclusión mutua y cierre del
transcodificador al desconectar. Esto no mide la fluidez en el Atom N270 real ni
la relación de bytes recibidos del modo directo.

Código propio bajo MIT; FFmpeg y yt-dlp conservan sus respectivas licencias.
