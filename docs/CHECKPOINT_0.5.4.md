# Checkpoint 0.5.4

Estado inicial releído: `main` en `bdd14aa` (ZIP 0.5.3), código `99a237e`.
La interfaz, fullscreen y presets ya estaban publicados; no se reaplicaron esos
cambios ni se usó Publicar del entorno. El objetivo de esta iteración es reducir
la espera al reproducir manteniendo Win32 x86/XP y las dependencias fijadas.

| Tarea | Estado | Evidencia | Siguiente |
| --- | --- | --- | --- |
| Medir y localizar demoras | Hecho | `evidence/0.5.4/startup-comparison.json`, `worker-official-comparison.json` | Medir en Atom real |
| Preparar VLC en paralelo e índice nativo | Hecho | `src/player.c`, `plugin-index.json`, 396 checks del controlador | Carpeta/pendrive XP real |
| Reutilizar motor Buscar→Reproducir | Hecho | `src/engine_worker.c`, plugin y 100 checks del actor; comparación oficial x64 | Validar RAM con 512 MB |
| Caché de dos URLs, sin guardar video | Hecho | `src/stream_cache.c`, 106 checks y GUI 96 | Primera apertura y reapertura reales |
| Cancelar, reintentar y cambios de calidad | Hecho offline | `final-validation.json` | Flujo online con motor XP |
| Build limpio reproducible y auditoría PE | Hecho | `reproducible-build.json`, `compatibility-audit.json` | Ejecutar el EXE x86 en XP |
| Clips 144/240/360 y Range | Hecho x64/Wine | `final-validation.json` | Sonido/fluidez físicos |
| Guía y notas | Hecho | `RELEASE_NOTES_0.5.4.md`, `PRUEBAS_MANUALES_0.5.4.md` | L1–L7, I1–I3, W1–W5 |
| ZIP y publicación GitHub | Cierre externo | `releases/CHECKPOINT_0.5.4.md` | Descargar y verificar SHA-256 |

Decisiones: conservar VLC; las precargas adicionales de DLLs/clip oculto no dieron
una ventaja consistente y se descartaron. La preparación solapada mejoró unos
100–200 ms en HTTP local con resolver simulado. Reutilizar yt-dlp evitó otro
arranque: 38 ms frente a 3444 ms de mediana al resolver datos fixture con un motor
nuevo. No se presentan esos números como tiempos reales de YouTube.

[SUPUESTO] Se conserva el motor hasta 12 s tras Buscar para equilibrar selección
y RAM, y el presupuesto del frontend se expresa en menos de 100 KiB. El frontend
final pesa 99 KiB; no se comprimió con un cargador ni se añadió un framework.

[NO EJECUTADO] XP/Atom/Windows físicos, YouTube real, RAM/paginación con 512 MB y
sonido audible. El framework XP y los avisos KERNELxp/ntext requieren comprobación
allí. El ZIP sigue superando la meta inicial de ~40 MB; ver THIRD_PARTY y fuentes.

Siguiente mejor acción: extraer el ZIP nuevo en XP/Atom, probar L1–L7, I1–I3 y
W1–W5. Registrar primer cuadro/sonido, PID y memoria en tres repeticiones. En
particular comprobar que la reutilización ahorra espera sin causar paginación.
