# Spatial binning: modo dislocaciones

Pedido del usuario (2026-11-02): "dale hace commit y continuemos". Esto continúa el último punto de
`research/pro_ports/docs/parameter_audit_2026-11-02.md`: "Spatial binning: modo dislocaciones [...] densidad escalar
(1/longitud²) y tensor de Nye por celda [...] sólo celdas ortogonales alineadas con los ejes".

## Especificación medida en OVITO Pro 3.16.1 (caja negra)

Los scripts y el CA de prueba están en el scratchpad de la sesión 54f22324.
- Kernel: interpolación trilineal (CIC) entre los centros de bin, en las direcciones binneadas. Es periódico e integra
  cada segmento de forma exacta (con 2 vértices da lo mismo que con 50 y coincide con la integral analítica).
- Densidad[bin] = Σ largo·peso / V_bin, con V_bin = V_celda / (número de bins). En 1D y 2D también se usa el volumen 3D.
- Nye[bin]_ij = Σ peso·largo·b_i·t_j / V_bin, con b = Burgers espacial (orientación del cluster × Burgers del cristal).
  Las componentes son XX..ZZ e invertir la línea cambia el signo.
- Salidas: en 1D, DataTable "binning" con título "Dislocations", y = "Density" y columna "Nye Tensor"; en 2D/3D, VoxelGrid
  "binning" con título "Binning (Dislocations)". Todo en float64.
- La primera derivada (sólo 1D) se aplica a las dos salidas, con diferencias centrales periódicas, y conserva los nombres.
- Errores: celda no ortogonal → "Dislocation field can only be calculated for simulation cells that are orthogonal and
  axis-aligned.The input simulation cell does not fulfill these conditions."; sin red de dislocaciones → "The input data
  collection contains no Dislocations data object."
- En direcciones no periódicas Pro sólo acepta puntos entre el primer y el último centro de bin (en un eje no binneado no
  acepta ninguno), aunque estén dentro de la celda. **Diferencia deliberada:** el fork acepta todo punto dentro de la celda y
  le da peso completo al bin del borde. Para puntos fuera de la celda usa el mismo mensaje de error que Pro.

## Diseño

- `particles`: clase abstracta `BinningLineSource` (lista de segmentos a, b y Burgers espacial a partir de un DataCollection)
  + `operateOn` {Particles, Dislocations} en `SpatialBinningModifier`. Se descubre con `PluginManager::metaclassMembers`,
  porque Particles no puede depender de CrystalAnalysis.
- `crystalanalysis`: `DislocationBinningLineSource` sobre `DislocationNetwork`.
- Integración exacta: cada segmento se parte en los cruces con planos de centros de bin; en cada tramo los pesos son
  polinomios de grado ≤ 3, así que Gauss-Legendre de 2 puntos es exacto.
- Editor: combo "Operate on"; los controles que sólo valen para partículas se deshabilitan.
- Test: ctest con el CA de prueba (3 líneas: una cruza el borde periódico, una diagonal 3D y un lazo) contra los valores de Pro en 1D X5, 2D XY43,
  3D XYZ543 y derivada.
