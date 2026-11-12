# Cierre de TubeLiviano 0.5.4

Código: `664ac4b96e142e65b1417da5740193ba4fdc8222`. Base publicada: `bdd14aa` (0.5.3).

| Tarea | Estado | Evidencia | Siguiente |
| --- | --- | --- | --- |
| Primer arranque | Implementado | Preparación VLC en paralelo, índice nativo portable | Cronometrar primer cuadro en XP/Atom |
| Buscar→Reproducir | Implementado y probado offline | Reutilización del motor hasta 12 s y respuesta antes de su terminación | Comparar RAM/paginación con 512 MB |
| Reapertura | Implementado | Dos enlaces/calidades probados, TTL 2 min; Reintentar fresco; recuperación de URL fallida/sin imagen | L5–L9 del checklist |
| Cancelación | Aprobada con fixtures | Árbol Job, bloqueo de stdin, cierre mientras prepara VLC | W4 en hardware real |
| Build x86 | Reproducible | Tres EXE idénticos de 101376 bytes (99 KiB), PE32/i386 XP 5.01 | Ejecutar binario entregable en XP |
| Regresiones | Aprobadas | 1692 checks x64/Wine + 30 tests host/ZIP sin omitir; plugin 70 y parser 38 adicionales | Pruebas físicas |
| VLC real | Aprobado bajo Wine | Clips 144/240/360, fullscreen visible y Range parcial | Sonido/fluidez reales |
| ZIP | Reproducible | Dos empaquetados idénticos; hashes debajo | Descargar/verificar |
| GitHub | Commit de artefactos y push explícito a main | `git push origin HEAD:refs/heads/main`; comprobar HEAD remoto tras push | No usar Publicar del entorno |

La ejecución integrada final está en `docs/evidence/0.5.4/final-validation.json`.
Su app x64 es `d7594c5b24ee9133e4b632ec14186472a812f2842f8b1ebd3c62c692ef5ff4b0`.
Ese binario de prueba no se distribuye como x86 ni certifica Windows físico.

## Artefactos

- [TubeLiviano_Portable_0.5.4_x86.zip](TubeLiviano_Portable_0.5.4_x86.zip): 53998945 bytes; SHA-256
  `4de53232fdcd5c46634352966b1e92241a8039cac42da713ca7311d985215489`.
- [TubeLiviano_ThirdParty_Sources_0.5.4.zip](TubeLiviano_ThirdParty_Sources_0.5.4.zip): 45711234 bytes; SHA-256
  `858ff9917564e5a4a479d43cf0eb36687b2e5bc4358eb53792bba0d8f0077813`.
- EXE x86: SHA-256 `39470649fa0316c687f5fef8eb60b08ca6a5f2128fd6651d5be5309d9414abae`.

El ZIP portable contiene VLC, ambos motores sin modificar, QuickJS, adaptador,
clips y código propio MIT. Los ZIP anteriores se conservan. Los informes de
empaquetado, reproducción determinista y tests host quedan fuera del ZIP para
no crear una referencia circular al checksum.

Verificar en Linux desde `releases/`:

```sh
sha256sum -c SHA256SUMS.txt --ignore-missing
```

Con Python disponible en otra PC, sin instalar nada en la netbook:

```sh
python3 -c "import hashlib,pathlib; p=pathlib.Path('TubeLiviano_Portable_0.5.4_x86.zip'); print(hashlib.sha256(p.read_bytes()).hexdigest())"
```

## Cambios y límites

Se conserva VLC 3: el mayor costo evitable medido era arrancar otra vez yt-dlp.
En tres muestras de integración con motor oficial x64 y extractor fixture offline,
resolver reutilizado dio 38 ms de mediana frente a 3444 ms al iniciar otro motor.
La terminación posterior a la respuesta fue asíncrona, mediana 446 ms. **No son
tiempos de extracción de YouTube**. El benchmark VLC HTTP con resolver simulado
mostró mejoras más pequeñas, unos 100–200 ms. No se adoptaron precargas ocultas
sin ganancia consistente ni se redujo a ciegas el búfer predeterminado.

[SUPUESTO] Se acepta retener el intérprete hasta 12 s tras Buscar, liberándolo
al resolver/cancelar y sin dejarlo residente durante el video. El presupuesto del
frontend es menos de 100 KiB; el test ahora expresa esa unidad binaria. LTO y la
omisión de descripciones GNU de unwind de C reducen tamaño sin tocar SEH x86 ni
los DLLs upstream; pueden reducir detalles de backtraces GNU.

[NO EJECUTADO] XP/Atom/Windows físicos, YouTube real, sonido audible y memoria/
paginación en 512 MB. El prototipo x64 observó ~84 MiB de working set del motor
idle; no se traslada ese valor a XP. Los errores KERNELxp/ntext siguen necesitando
prueba allí. El proxy del entorno bloquea YouTube con 403; no se observó un 429
real. El paquete ronda 54 MB y no cumple la meta original de ~40 MB; se conservan
los límites de fuentes transitivas de THIRD_PARTY.md.

Siguiente mejor acción: extraer completo en una carpeta nueva; ejecutar L1–L7,
I1–I3 y W1–W5 de `source/docs/PRUEBAS_MANUALES_0.5.4.md`. Primero probar imagen y
audio offline; después buscar y seleccionar un resultado antes de 12 s. Comparar
primera apertura/reapertura/Reintentar en tres repeticiones y observar procesos y RAM.
