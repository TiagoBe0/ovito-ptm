# Spatial binning, modo dislocaciones: caja negra contra OVITO Pro 3.16.1

Correr con el `ovitos` de Pro desde este directorio. Pro descarta el stdout, así que cada script escribe su log en el archivo que se le pasa como segundo argumento.
- `mk_ca.py test.ca`: genera el CA de prueba. Es el mismo que escribe `writeCA()` en `SpatialBinningSelfTestService.cpp`.
- `ref.py test.ca ref.log`: produce los valores de referencia de Pro que están embebidos en el ctest (1D X5, 2D XY43, 3D XYZ543 y derivada).
- `e1.py`–`e6.py real.ca out.log` (`lib.py` es el helper): experimentos sobre un CA exportado por el DXA de Pro a partir de
  `research/dislocation_tracking/data/calib_edge_Au/dump.known_glide.lammpstrj` (frame 0):
  - e1: kernel CIC periódico entre centros de bin;
  - e2: integración exacta a lo largo del segmento;
  - e3: kernel trilineal 3D, Nye = b_espacial ⊗ t, triclínica;
  - e4/e5: ejes no periódicos (Pro sólo acepta puntos entre el primer y el último centro de bin);
  - e6: metadatos de salida.
