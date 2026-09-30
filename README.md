# Planificador Dieciochero

Tarea 1 — Sistemas Operativos 

## Integrantes

- Joaquín Utreras
- Jeremias Quezada

## Descripción

Simulador y planificador de actividades para las Fiestas Patrias del señor
Loyola. Lee un plan de actividades descrito como un grafo acíclico dirigido
(DAG) en un archivo de texto, y las ejecuta como procesos reales (`fork()`),
respetando el orden de dependencias entre ellas y un límite máximo de
procesos concurrentes (K).

## Compilación

```bash
gcc -Wall -Wextra -std=c17 -lpthread -o planificador src/planificador.c
```

(también se puede usar `make`, que ejecuta este mismo comando).

No se usan hilos ni mecanismos de sincronización de hilos en ningún punto
del programa, tal como exige el enunciado; el flag `-lpthread` se incluye
solo porque la rúbrica lo pide en la línea de compilación.

## Uso

```bash
./planificador plan.txt K
```

Donde `plan.txt` es el archivo con las actividades (ver formato más abajo)
y `K` es el número máximo de procesos que pueden estar corriendo al mismo
tiempo.

### Formato de plan.txt

ID_Actividad : Nombre_Actividad : tiempo_ms : Dependencia1, Dependencia2, ...


Si `tiempo_ms` se deja vacío, se asigna un valor aleatorio entre 100 y 5000 ms.
Si la lista de dependencias se deja vacía, la actividad no depende de nada.

### Variables de entorno para pruebas

- `FALLO_ID=<id>`: fuerza a que la actividad con ese ID falle siempre (útil
  para probar el aislamiento de errores de forma predecible).
- `SIMULAR_FALLOS=1`: activa una probabilidad aleatoria del 5% de que
  cualquier actividad falle. Sin esta variable, ninguna actividad falla al
  azar (solo con `FALLO_ID` se puede forzar una falla puntual).

## Funciones implementadas

**Parseo (`leer_linea`, `quitar_espacios`)**: lee `plan.txt` línea por línea
con `fgets`, separa los 4 campos buscando los `:` manualmente con `strchr`
(no se usa `strtok` para esto porque colapsa campos vacíos consecutivos,
y el formato permite dejar el tiempo o las dependencias vacíos). Asigna
tiempo aleatorio cuando corresponde.

**Construcción del DAG (`construir_grafo`, `buscar_indice`)**: convierte los
IDs de texto de las dependencias en índices dentro del arreglo de
actividades, arma para cada actividad la lista de sus "sucesores" (quiénes
dependen de ella), y valida que no haya IDs repetidos ni dependencias hacia
actividades inexistentes.

**Detección de ciclos (`hay_ciclo`)**: simula el algoritmo de Kahn (el mismo
que después usa el planificador para saber qué actividades están listas):
si al final de la simulación no se "procesaron" todas las actividades,
es porque hay un ciclo.

**Creación de procesos (`ejecutar_actividad`)**: cada actividad corre en su
propio proceso hijo (`fork()`), que simula su duración con `usleep()` y
termina con `exit()`.

**Planificador con límite K (`ejecutar_planificador`)**: mantiene una cola
de actividades listas para ejecutar (dependencias cumplidas) y lanza
procesos mientras haya espacio (`activos < K`). Cuando no puede lanzar más,
se bloquea en `wait()` esperando a que **cualquier** hijo termine — esto es
bloqueante, no hay ningún ciclo que consuma CPU preguntando repetidamente
(sin busy-waiting). Al terminar un hijo, se actualizan los contadores de
dependencias de sus sucesores y se encolan los que queden listos.

**Paso de mensajes (pipes)**: antes de lanzar una actividad, se crea un pipe
por cada uno de sus sucesores (deben existir antes del `fork()` para que el
hijo herede los descriptores). Al terminar, cada actividad escribe un
mensaje de texto por esos pipes; sus dependientes lo leen apenas arrancan,
antes de empezar su propio trabajo.

**Aislamiento de errores (`marcar_abortada`)**: si una actividad falla
(sale con código distinto de 0), el planificador no se cae: marca como
"abortada" a toda su rama de dependientes (directos e indirectos, con un
recorrido por cola sobre el grafo de sucesores) y esas actividades nunca
se llegan a lanzar. El resto del plan, en ramas no afectadas, continúa
normalmente.

**Ctrl+C (`manejar_sigint`)**: se captura `SIGINT` con `sigaction` (sin
`SA_RESTART`, para que una llamada bloqueada en `wait()` se interrumpa).
Al recibir la señal, el planificador manda `SIGTERM` a todos los procesos
hijos que sigan corriendo y los espera con `waitpid` para no dejar procesos
zombies, y termina de forma controlada.

## Decisiones de diseño

- **Arreglos de tamaño fijo** (hasta 10000 actividades, 50 dependencias por
  actividad) en vez de estructuras dinámicas más complejas: el enunciado
  fija ese máximo, y simplifica bastante el código.
- **Cola de actividades listas + arreglos paralelos de procesos corriendo**
  (`pids_corriendo`/`idx_corriendo`) con la técnica "swap-remove" (al sacar
  un proceso de la lista de corriendo, se reemplaza por el último y se
  reduce el contador) para no tener que desplazar todo el arreglo cada vez.
- **`srand(getpid())` en cada hijo**: como `fork()` copia el estado interno
  de `rand()`, sin esto todos los hijos generarían la misma secuencia de
  números "aleatorios" (todos heredan el mismo estado del padre al momento
  de nacer).
- **Simulación de fallas configurable** (`FALLO_ID` / `SIMULAR_FALLOS`) en
  vez de una probabilidad fija siempre activa: permite probar el
  aislamiento de errores de forma determinista, y desactivar las fallas
  al azar para la prueba de carga (10000 actividades fallando al 5% habría
  abortado la mayor parte del plan antes de tiempo).
- **`signal(SIGPIPE, SIG_IGN)`**: si por algún motivo se escribe en un pipe
  sin lectores, preferimos que `write()` falle en silencio a que el proceso
  muera por la señal por defecto.

## Pruebas realizadas

- Casos base: grafo del enunciado (`plan.txt`), detección de ciclo
  (`plan_ciclo.txt`) y dependencia inexistente (`plan_malo.txt`).
- Verificación con AddressSanitizer (`-fsanitize=address`) en todos los
  pasos, sin errores de memoria.
- Aislamiento de errores probado con `FALLO_ID` en distintos puntos del
  grafo, confirmando que solo se aborta la rama afectada.
- Ctrl+C probado a mitad de ejecución: todas las actividades corriendo se
  abortan, sin dejar procesos zombies (confirmado con `ps aux`).
- Prueba de carga: `generar_estres.py` genera un plan de 10000 actividades
  (20 cadenas paralelas independientes). Ejecutado con `K=20`, completa las
  10000 actividades en ~5.4 segundos, con código de salida 0, y sin
  necesidad de aumentar el límite de descriptores de archivo del sistema
  (probado con el valor por defecto, `ulimit -n` = 256).

## Limitaciones conocidas

- Máximo 50 dependencias por actividad (`MAX_DEPS`).
- La búsqueda de actividades por ID (`buscar_indice`) es lineal; suficiente
  para la escala probada (10000 actividades), pero no está optimizada con
  una tabla hash.
- La prueba de carga usa una estructura donde cada actividad tiene como
  máximo una dependencia y un sucesor; los casos de múltiples dependencias
  y sucesores por actividad se probaron en el ejemplo del enunciado, no a
  gran escala.
