# Manual técnico: detección de anillos cerrados en carbono amorfo

Este documento explica, en profundidad y para estudiar, el trabajo hecho sobre
`AmorphousCarbonModifier` y en particular la detección de anillos cerrados
(closed rings) agregada más recientemente. La idea es que sirva como material
de estudio propio: no solo qué se hizo, sino **por qué** se hizo así, qué
alternativas se descartaron y por qué, y cómo se sabe que el resultado es
correcto sin tener una implementación de referencia para comparar.

El documento técnico "de uso" (parámetros, propiedades, atributos, modo
batch) es `AMORPHOUS_CARBON_MODIFIER.md`. Este manual no lo repite en detalle;
en cambio, explica el razonamiento detrás de cada decisión, con la profundidad
necesaria para responder preguntas de un director de tesis.

---

## 1. Contexto: ¿qué problema se está resolviendo?

`AmorphousCarbonModifier` es uno de los tres módulos nativos de este fork de
OVITO (junto con seguimiento de dislocaciones y detección de vacancias). Sirve
para analizar configuraciones de carbono amorfo (a-C) bajo daño por radiación
simulado con mCRA (metadynamics/molecular dynamics Cyclic Relaxation
Annealing, o el protocolo equivalente que se esté usando —
`CG + NVT 300K + CG final` por ciclo).

El modificador hace dos cosas, en dos etapas históricas:

1. **Clasificar** cada átomo de carbono por hibridación (sp, sp2, sp3) según
   su número de coordinación, y **agrupar en clusters** (componentes
   conexas) los átomos de una misma fase (sp2 o sp3) que están enlazados
   entre sí.
2. **(Nuevo)** Detectar **anillos cerrados** en la red de enlaces de esa
   fase, y contar de cuántos átomos está compuesto cada uno.

La pregunta que motivó el punto 2 es muy concreta: al clusterizar sp3 con
ciertos cutoffs, a simple vista aparecen formas que parecen anillos cerrados
de carbono. La pregunta del usuario fue: **¿se puede formalizar esto —
aislar cada anillo y contar sus átomos, en lugar de mirarlo a ojo?**

---

## 2. ¿Por qué esto no es trivial? El problema de contar "anillos"

### 2.1 Un anillo es un ciclo en un grafo

Si pensás la red de enlaces C–C como un grafo (átomos = nodos, enlaces =
aristas), un "anillo cerrado" es un **ciclo simple**: una secuencia de átomos
`a1 - a2 - ... - an - a1` donde cada enlace consecutivo existe y ningún átomo
se repite.

El problema es que en una red **densa y percolante** como el sp3 de a-C (el
mismo documento existente ya mide esto: la red sp3 percola en los 101 ciclos
de `camorfo_sp3_60_mCRA`, con grado interno medio ~2.6–3.6), **hay
combinatoriamente demasiados ciclos**. Cualquier par de ciclos pequeños que
comparten un enlace se puede "sumar" para formar un ciclo más grande (por
ejemplo, dos hexágonos fusionados por una arista también forman, en conjunto,
un ciclo de 10 átomos bordeando ambos). Si contás *todos* los ciclos posibles
hasta un tamaño N, el número crece exponencialmente y la enorme mayoría de
esos ciclos "grandes" no son anillos reales — son la combinación algebraica
de anillos más chicos.

**Esto es la razón número uno por la que "contar todos los ciclos cerrados"
no es lo que se usa en la práctica ni en la literatura.** Necesitás un
criterio que aísle los ciclos "básicos" — los que no se pueden descomponer en
ciclos más chicos.

### 2.2 Alternativas consideradas (y por qué se descartaron)

| Criterio | Qué hace | Por qué no se usó como principal |
|---|---|---|
| **Todos los ciclos hasta tamaño N** | Enumera literalmente cualquier ciclo cerrado | Explosión combinatoria en el núcleo sp3 denso (miles de ciclos por átomo); no es lo que se reporta en la literatura como "anillos" |
| **Anillos más cortos de King** (King, 1967) | Para cada enlace, el ciclo más corto que lo contiene, sin filtro adicional | Más simple, pero puede sobrecontar: un ciclo que en realidad es la suma de dos anillos más chicos que comparten un enlace puede pasar como "el más corto" en redes con cierta geometría |
| **Anillos primitivos / SP (Franzblau, 1991)** ✅ | El ciclo más corto por enlace, *más* un test de que ningún par de átomos del anillo tiene un camino más corto por el resto de la red | Es el estándar de la literatura de estadística de anillos en carbono amorfo y sílice amorfa. Rechaza específicamente los "anillos" que son la suma de dos más chicos |

Se eligió el criterio de **Franzblau (SP / shortest-path / primitive rings)**
porque:

- Es el que efectivamente se usa en papers de estadística de anillos de
  materiales amorfos tetraédricos (carbono, silicio, sílice).
- Tiene una definición matemática precisa y verificable (no depende de
  parámetros arbitrarios de "qué tan primitivo" es un anillo).
- Da un número acotado y estable, no combinatoriamente explosivo.

**Referencia**: D. S. Franzblau, *Phys. Rev. B* **44**, 4925 (1991).

---

## 3. El criterio SP / anillo primitivo, explicado paso a paso

### 3.1 Definición matemática

Para cada enlace `(i, j)` de la red:

1. Buscar el camino más corto entre `i` y `j` **que no use ese enlace
   directamente** (es decir, en el grafo sin esa arista). Llamemos `d` a la
   longitud de ese camino (en número de enlaces).
2. El ciclo formado por ese camino más el enlace `(i,j)` que lo cierra tiene
   tamaño `d + 1` átomos.
3. **Test de primitividad**: ese ciclo se acepta como "anillo" solo si, para
   *cualquier par* de átomos `(a, b)` sobre el ciclo, la distancia más corta
   entre `a` y `b` **en toda la red** (no solo dentro del ciclo) es igual a
   la distancia que se recorre yendo por el arco más corto del ciclo. Si
   existe un atajo más corto por fuera del ciclo, el ciclo se descarta: es
   la suma de dos anillos más chicos, no un anillo real.

### 3.2 Ejemplo intuitivo

Imaginate dos hexágonos (6 átomos cada uno) fusionados por una arista común
(como en el naftaleno, dos anillos bencénicos pegados). El borde exterior de
esa figura, recorrido completo, también es un "ciclo cerrado" de 10 átomos.
Pero ese ciclo de 10 **no** es un anillo primitivo: para cualquier par de
átomos en lados opuestos de ese borde, hay un atajo más corto cruzando por la
arista compartida (el "puente" entre los dos hexágonos). El test de
primitividad detecta exactamente eso y descarta el ciclo de 10, dejando
correctamente los dos hexágonos de 6.

### 3.3 Empates (varios caminos igual de cortos)

En una red muy simétrica (como una porción de red diamante perfecta), puede
haber **varios** caminos distintos de la misma longitud mínima entre `i` y
`j`. El algoritmo de Franzblau exige revisar **todos** los empates, no
cualquiera arbitrario — cada uno puede o no pasar el test de primitividad, y
puede corresponder a un anillo físicamente distinto. La implementación hace
esto: encuentra todos los caminos de longitud mínima (acotado a 32
candidatos por enlace, para no explotar en regiones perfectamente
simétricas) y evalúa cada uno por separado.

### 3.4 Condiciones de contorno periódicas: cerrar "en el espacio real"

Esta es una sutileza que no aparece en los papers de Franzblau (que trabajan
con sistemas finitos o ya "desenvueltos"), pero es crítica en simulación con
periodicidad:

En una celda periódica, dos átomos pueden estar "enlazados" a través de una
imagen periódica de la celda (el mismo truco que ya usa el análisis de
percolación de clusters, en `AmorphousCarbonModifier.cpp`). Si no se tiene
cuidado, un camino que en realidad **da la vuelta completa a la celda**
(un lazo de percolación) podría contarse como si fuera un "anillo molecular"
cerrado, cuando en realidad es solo una manifestación de que la red conecta
consigo misma a través del borde periódico — no una molécula cerrada real.

La implementación lleva la cuenta del **desplazamiento entero de imagen
periódica** (`Vector3I`, igual que hace el test de percolación) a lo largo
de cada camino candidato, y solo acepta el ciclo si esa cuenta da
exactamente cero al cerrar el lazo. En la práctica esto es casi siempre
irrelevante: un anillo de 8 átomos mide como mucho ~15-20 Å de perímetro,
muy por debajo de una celda de simulación típica de ~150+ Å, así que un
anillo real jamás está cerca de "dar la vuelta" a la celda. Pero es
matemáticamente necesario para que el resultado sea correcto en el caso
general, y es exactamente el mismo tipo de cuidado que ya se usaba en el
código de clustering existente para detectar percolación.

---

## 4. El algoritmo, en términos de implementación (sin código)

Para cada enlace de la fase seleccionada (sp2 o sp3):

1. **Búsqueda en anchura (BFS) acotada** desde uno de los átomos del enlace,
   prohibiendo usar ese enlace directo como primer paso, hasta una
   profundidad máxima determinada por el parámetro `maxRingSize` (si el
   anillo más grande que interesa tiene N átomos, el camino alternativo más
   largo que hay que buscar tiene N-1 enlaces).
2. Si se encuentra el otro extremo del enlace a alguna profundidad, se
   reconstruyen todos los caminos empatados a esa profundidad mínima
   (usando una estructura de "predecesores" tipo árbol de búsqueda).
3. Para cada camino candidato: se verifica el cierre en espacio real
   (sección 3.4) y el test de primitividad (sección 3.1, punto 3).
4. Los anillos que pasan ambos tests se **deduplican**: el mismo anillo
   físico puede encontrarse desde cualquiera de sus enlaces, así que se
   normaliza la secuencia de átomos (rotarla para empezar en el índice más
   chico, elegir la dirección que da la secuencia lexicográficamente menor)
   y se guarda en un conjunto (`unordered_set`) para no contarlo dos veces.

### 4.1 ¿Por qué es viable en un sistema de 473.000 átomos?

La preocupación obvia es de costo computacional: hacer una búsqueda acotada
desde **cada uno** de los ~600.000-1.000.000 de enlaces de la fase sp3 podría
ser muy lento. Tres decisiones lo hacen tratable:

- **Profundidad acotada por `maxRingSize`**: no se busca "todo lo alcanzable
  desde cada átomo", solo hasta la profundidad que podría cerrar un anillo
  del tamaño máximo configurado (por defecto 8, o sea profundidad máxima 7).
- **Corte apenas se encuentra el cierre**: en cuanto la capa de BFS actual
  contiene el átomo buscado, se detiene — no se sigue expandiendo.
- **Estructuras reutilizadas entre búsquedas** (contadores de "generación"
  en vez de reinicializar arreglos de tamaño N en cada una de las ~10^6
  búsquedas — eso sí sería inviable).

El resultado medido: sobre la configuración completa de 473.344 átomos, la
detección de anillos agrega **~10 segundos** sobre el análisis de clusters
existente (que de por sí toma ~10 segundos). Es notablemente rápido porque
en una red densa y bien conectada, la mayoría de los enlaces cierran su
anillo primitivo en muy pocos saltos.

---

## 5. Parámetros y qué controlan

| Parámetro | Rol | Default |
|---|---|---|
| `Maximum ring size to search` (`maxRingSize`) | Profundidad de búsqueda; principal control de costo. Anillos reales en a-C casi siempre tienen <10 átomos. | 8 |
| `Minimum ring size to search` (`minRingSize`) | Filtra anillos por debajo de este tamaño **después** de detectarlos (p. ej. para excluir triángulos). No puede bajar de 3: un "anillo" de 1 o 2 átomos no existe en un grafo simple. | 3 |
| `Detect closed rings (SP/primitive)` | Prende/apaga el cálculo (es costoso, así que está apagado por defecto). Elegir `Color by ring size` también lo activa automáticamente. | apagado |

**Importante sobre `minRingSize`**: no busca "el siguiente anillo más
grande" cuando descarta uno chico — simplemente no lo cuenta. Esto es
coherente con el criterio de Franzblau: cada enlace contribuye **como mucho**
su propio anillo primitivo más chico (o el conjunto empatado), no una lista
de anillos de distintos tamaños por el mismo enlace.

---

## 6. Salidas del modificador

Por átomo:

- `Ring Size`: tamaño del anillo primitivo más chico al que pertenece ese
  átomo (0 si no pertenece a ninguno).
- `Smallest Ring Identifier`: ID de ese anillo, para poder aislarlo con
  Expression Selection (`SmallestRingIdentifier==42`) y contar exactamente
  sus átomos.
- `Ring Membership Count`: a cuántos anillos primitivos distintos pertenece
  el átomo (un átomo compartido por dos anillos fusionados vale 2).

Atributos globales (`AmorphousCarbon.ring_*`): cantidad total de anillos,
tamaño medio, histograma de cantidad por tamaño, y eco de los parámetros de
búsqueda usados.

---

## 7. Validación: ¿cómo se sabe que esto es correcto?

Este es probablemente el punto que más te va a preguntar tu director, porque
**no hay una implementación de referencia en Python contra la cual
comparar** (a diferencia del análisis de clusters, que sí se validó contra
`ac_analyzer.analysis.sp2_clusters`). La validación se hizo por **hechos
físicos conocidos**, no por comparación numérica directa:

### 7.1 El resultado más fuerte: coincidencia con la red del diamante

En el ciclo 0 de `camorfo_sp3_60_mCRA` (la configuración menos dañada, más
parecida a diamante), el tamaño medio de los anillos primitivos sp3 da
**6.004 átomos**. La red de diamante ideal está compuesta **enteramente**
por anillos de 6 átomos (conformación silla, igual que el ciclohexano). Que
la medición dé 6.00 a tres cifras significativas, sin haber ajustado nada
para que eso pase, es una confirmación muy fuerte de que el algoritmo está
contando lo que dice contar.

### 7.2 Tendencias físicas esperadas

| | ciclo 0 (menos dañado) | ciclo 100 (más dañado) |
|---|---|---|
| población sp3 | 298.356 átomos | 264.436 átomos |
| anillos primitivos sp3 | 60.915 | 32.485 |
| tamaño medio | 6.00 | 5.87 |

El daño por radiación introduce defectos (anillos de 5 átomos, típicos de
redes tetraédricas dañadas — el mismo tipo de defecto que se ve en silicio
amorfo irradiado), así que el tamaño medio **debería** bajar con el daño.
Baja. La cantidad de anillos también baja, consistente con la caída de
población sp3. Ambas son exactamente las direcciones físicamente esperadas.

La fase sp2 en el ciclo 100 (fragmentada, cluster más grande de solo 22
átomos — lejos de ser una lámina grafítica intacta) da muchos menos anillos
(63, media 5.22) que la matriz sp3 percolante — también coherente: sin una
red extendida no hay muchos hexágonos reales para encontrar.

### 7.3 Consistencia interna: monotonía de `minRingSize`

Al subir el piso de tamaño de 3 a 4 a 6 (sobre la misma configuración), la
cantidad de anillos solo puede bajar o quedarse igual, y el tamaño medio solo
puede subir o quedarse igual (porque se están quitando los valores más
chicos de la distribución). Medido: 32.485 → 32.404 → 18.797 anillos, media
5.87 → 5.88 → 6.54. Se comporta exactamente así. Esto no prueba que el
algoritmo sea correcto en términos absolutos, pero descarta una clase grande
de bugs (si subir el piso hiciera *subir* el conteo, o bajar la media, sería
una señal inequívoca de error).

### 7.4 Qué NO es una validación (y por qué)

No hay un test unitario aislado tipo "caso sintético a mano" para este
algoritmo específico (sí existe para el emparejamiento de clusters del
modificador de tracking, ver `AMORPHOUS_CARBON_MODIFIER.md`). Es una
limitación real, no una omisión menor, y hay que poder decirlo así si te lo
preguntan: la confianza actual viene de (a) revisión manual cuidadosa del
álgebra de cierre periódico, (b) los hechos físicos de la sección 7.1-7.2, y
(c) la consistencia interna de la sección 7.3. Un test sintético (por
ejemplo, construir a mano una red pequeña con anillos de tamaño conocido,
incluyendo un caso con "anillo compuesto" que debe ser rechazado por el test
de primitividad) sería el siguiente paso natural si se quiere reforzar esto
antes de publicar.

---

## 8. Un caso real de error de interpretación (para no repetirlo)

Durante el desarrollo surgió una confusión que vale la pena entender bien,
porque es fácil cometerla de nuevo: se reportó que "con los mismos
parámetros, el ciclo 0 no muestra anillos pero el ciclo 100 sí muestra
muchos". Al investigar con los parámetros exactos usados (coordination
cutoff 1.9, bond cutoff 2.3), el resultado cuantitativo real es el
**opuesto**: ciclo 0 tiene 57.687 anillos primitivos, ciclo 100 tiene 38.285.

¿Qué pasó entonces? Dos cosas, ninguna un bug:

1. **Se estaban confundiendo dos conceptos distintos.** Antes de tener
   `Ring Size`, la única forma de "ver anillos" era mirar clusters chicos
   que por casualidad tenían forma de anillo. Con esos cutoffs, en el ciclo
   0 el 99.6% de la fase sp3 está en **un solo cluster gigante** — no hay
   subestructura chica para distinguir a simple vista. En el ciclo 100, la
   misma red se fragmentó en 1.773 clusters, muchos chicos y con forma
   reconocible de anillo. Eso hace que el ciclo 100 "se vea con más
   anillos" — pero es un efecto de fragmentación visual de clusters, **no**
   una medición de anillos topológicos reales.
2. **El cutoff de enlace (2.3) es sustancialmente más largo que el de
   coordinación (1.9).** Esto ya estaba documentado como un error a evitar
   ("Why the two cutoffs match by default" en `AMORPHOUS_CARBON_MODIFIER.md`)
   para el conteo de clusters, pero acá se ve con una magnitud severa: entre
   209.000 y 273.000 átomos (más o menos la mitad de la fase sp3) tienen un
   enlace en el grafo que nunca contó para definir su hibridación. Esto no
   causó el efecto de arriba, pero significa que ni los clusters ni los
   anillos calculados con esos cutoffs describen exactamente la red que
   definió qué es sp3.

**La lección para explicarle a tu director**: "ver formas de anillo en el
render" no es lo mismo que "hay más anillos reales". Para cualquier número
que se vaya a reportar, hay que usar `Ring Size` / `ring_count_total`, no la
impresión visual de qué tan fragmentados se ven los clusters.

---

## 9. Preguntas frecuentes (preparación para el director)

**P: ¿Por qué eligieron este criterio de anillo y no otro?**
R: Porque es el estándar publicado para estadística de anillos en redes
amorfas tetraédricas (carbono, silicio, sílice) — Franzblau 1991. Contar
"todos los ciclos" es combinatoriamente inviable en una red percolante, y
"el ciclo más corto por enlace" sin el test de primitividad (King) puede
sobrecontar anillos que en realidad son la suma de dos más chicos.

**P: ¿Cómo saben que el algoritmo está bien implementado si no hay una
referencia externa?**
R: Por un resultado físico verificable de forma independiente: la red menos
dañada (ciclo 0) da un tamaño medio de anillo de 6.004, que es exactamente
lo que predice la estructura de diamante ideal (todos los anillos son
hexágonos). Además, las tendencias con el daño (menos anillos, tamaño medio
más chico) y entre fases (sp2 fragmentado vs sp3 percolante) van en la
dirección físicamente esperada, y el filtro de tamaño mínimo se comporta de
forma monótona como debería.

**P: ¿Qué pasa con la periodicidad de la celda de simulación?**
R: Se exige explícitamente que el anillo cierre en espacio real (el
desplazamiento neto de imagen periódica alrededor del lazo debe ser cero),
para no confundir un anillo molecular real con un camino que da la vuelta a
la celda por percolación. En la práctica, para anillos de hasta ~10 átomos
en una celda de cientos de Å, esto nunca es un problema real, pero es
necesario para que el algoritmo sea correcto en el caso general.

**P: ¿Cuánto cuesta computacionalmente?**
R: ~10 segundos extra sobre 473.344 átomos (dado que el análisis de
clusters de por sí toma ~10 segundos), acotado principalmente por
`maxRingSize`, porque la búsqueda es de profundidad limitada y se corta en
cuanto encuentra el cierre.

**P: ¿Qué limitaciones tiene?**
R: (1) No hay un test unitario sintético aislado todavía, solo validación
por hechos físicos y consistencia interna. (2) Los empates de caminos más
cortos y los predecesores por nodo están acotados (32 y 64
respectivamente) para evitar explosión en regiones perfectamente
simétricas — un límite blando, pensado para no activarse en una red
genuinamente amorfa. (3) Depende de que los dos cutoffs (coordinación y
enlace) estén bien elegidos; un descalce grande entre ellos (como el caso
de la sección 8) hace que ni los clusters ni los anillos representen
fielmente la red que define la hibridación sp2/sp3.

**P: ¿Por qué no cuentan directamente triángulos, cuadrados, etc. como
"tipos" de defecto?**
R: Sí se puede: `Ring Size` da el tamaño de cada anillo por átomo, y
`AmorphousCarbon.ring_count_<N>` da el histograma completo por tamaño. El
parámetro `minRingSize` además permite excluir tamaños que se consideren
ruido (por ejemplo, triángulos, que son muy raros y muy tensionados en una
red tetraédrica real — en `camorfo_sp3_60_mCRA` ciclo 100 solo 81 de 32.485
anillos son triángulos).

---

## 10. Glosario rápido

- **Hibridación (sp, sp2, sp3)**: estado de coordinación de un átomo de
  carbono, determinado acá por su número de vecinos dentro de un cutoff
  (2 → sp, 3 → sp2, 4 → sp3).
- **Cluster / componente conexa**: conjunto de átomos de la misma fase
  conectados entre sí por una cadena de enlaces.
- **Percolación**: cuando un cluster se conecta consigo mismo a través del
  borde periódico de la celda — el cluster "atraviesa" la celda de lado a
  lado.
- **Anillo primitivo / SP ring**: ciclo cerrado en el grafo de enlaces que
  no se puede escribir como la suma de dos ciclos más chicos que comparten
  una arista (criterio de Franzblau).
- **BFS (breadth-first search / búsqueda en anchura)**: algoritmo que
  explora un grafo capa por capa desde un nodo de partida, garantizando
  encontrar el camino más corto en número de saltos.
- **Desplazamiento de imagen periódica (`pbcShift`)**: cuántas veces (y en
  qué dirección) hay que trasladar la celda de simulación para que dos
  átomos conectados por condiciones de contorno periódicas queden
  geométricamente adyacentes.

---

## Próximos pasos posibles para este manual

Este primer documento cubre solo la parte de carbono amorfo / anillos, que
es lo más reciente. Si querés, el siguiente paso natural sería un capítulo
equivalente para el seguimiento de dislocaciones o la detección de
vacancias — avisame y seguimos con esa sección.
