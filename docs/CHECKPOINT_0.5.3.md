# Checkpoint 0.5.3

Base publicada: `385d975` (0.5.2). Alcance autorizado: botón accesible de pantalla
completa y distintos presets en Configuración.

- Pantalla completa nativa: mismo HWND/reproductor, botón en Resultados, F11/Esc,
  pausa/cancelación, restauración normal/maximizada y tamaño portable conservado.
- Cinco presets reales, con Preparar/Guardar/Cancelar, manteniendo preferencias
  ajenas y tokens del puente solo en memoria. Atom 240p sigue predeterminado.
- Build limpio: tres EXE x86 idénticos de 89.088 bytes. Hash y comandos en
  `evidence/0.5.3/reproducible-build.json`.
- Validación: 1296 checks sin fallos al combinar suites actuales sobre el mismo
  binario x64; medios reales 144/240/360p, Range y fullscreen visible aprobados.
  Detalle y métodos de repetición en `RELEASE_NOTES_0.5.3.md` y evidencia JSON.
- Se cerraron las ramas de fixture que no mejoraban el producto: dos observaciones
  del bookkeeping pre-maximizado de Wine no se trataron como fallo de fullscreen;
  se conservaron todas las aserciones de entrada/salida y se documenta su alcance.
- ZIP reproducible, pruebas posteriores y commit/push se registran en el
  [cierre de publicación](https://github.com/kbdfh98uv/TubeLiviano/blob/main/releases/CHECKPOINT_0.5.3.md).

[SUPUESTO] Los presets se orientan a rendimiento/conexión. No se ofrecen más de
360p y no se sube la calidad predeterminada.

[NO EJECUTADO] XP/Atom físicos, RAM/fluidez/sonido audible y YouTube real. El
framework XP conserva los binarios fijados y requiere prueba en su equipo.

Siguiente mejor acción: extraer nuevo ZIP, ejecutar F1–F8 y P1–P5 de
`PRUEBAS_0.5.3.md`, primero con clips offline. Conservar error exacto y JSON.
