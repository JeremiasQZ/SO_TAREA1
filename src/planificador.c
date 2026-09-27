#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

/* Planificador*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_ACT 10000 /* Maximo de actividades */
#define MAX_DEPS 50 /* Maximo de dependencias por actividad */
#define MAX_ID 32
#define MAX_NOMBRE 64 
#define MAX_LINEA 4096

typedef struct {
    char id[MAX_ID];
    char nombre[MAX_NOMBRE];
    int tiempo_ms;
    int num_deps;
    char deps[MAX_DEPS][MAX_ID];

    int dep_idx[MAX_DEPS]; /* Las mismas dependencias como indices*/
    int pendientes; /* Cuantas dependencias faltan por terminar*/
    int *sucesores; /* indice de quienes dependen de esta*/
    int num_sucesores;
    int pipes_in[MAX_DEPS]; /* pipes para recibir insumos de dependencias */
} Actividad;

Actividad actividades[MAX_ACT];
int total = 0;

/* Control de qué procesos hijos están corriendo ahora mismo */
pid_t pids_corriendo[MAX_ACT];
int   idx_corriendo[MAX_ACT];
int   activos = 0;

/* Cola de actividades listas para lanzar (pendientes == 0, aún no lanzadas) */
int cola_listas[MAX_ACT];
int frente = 0, atras = 0;

/* Quita espacios, tabs y saltos de línea al inicio y al final.
   Devuelve un puntero al primer carácter útil. */
char *quitar_espacios(char *s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    int largo = strlen(s);
    while (largo > 0 && (s[largo - 1] == ' '  || s[largo - 1] == '\t' ||
                         s[largo - 1] == '\n' || s[largo - 1] == '\r')) {
        s[largo - 1] = '\0';
        largo--;
    }
    return s;
}

/* Lee una línea "ID : nombre : tiempo : deps" y la guarda en actividades[total].
   Devuelve 1 si la línea estaba bien, 0 si no. */
int leer_linea(char *linea) {
    char vacio[1] = "";

    /* 1) Cortar la línea en sus campos, buscando los ':' uno por uno */
    char *dos_puntos1 = strchr(linea, ':');
    if (dos_puntos1 == NULL) return 0;
    *dos_puntos1 = '\0';                  /* termina el campo ID */
    char *campo_nombre = dos_puntos1 + 1;

    char *dos_puntos2 = strchr(campo_nombre, ':');
    if (dos_puntos2 == NULL) return 0;
    *dos_puntos2 = '\0';                  /* termina el campo nombre */
    char *campo_tiempo = dos_puntos2 + 1;

    char *campo_deps = vacio;             /* por defecto: sin dependencias */
    char *dos_puntos3 = strchr(campo_tiempo, ':');
    if (dos_puntos3 != NULL) {
        *dos_puntos3 = '\0';              /* termina el campo tiempo */
        campo_deps = dos_puntos3 + 1;
    }

    /* 2) Limpiar espacios de cada campo */
    char *id     = quitar_espacios(linea);
    char *nombre = quitar_espacios(campo_nombre);
    char *tiempo = quitar_espacios(campo_tiempo);
    campo_deps   = quitar_espacios(campo_deps);

    if (id[0] == '\0' || nombre[0] == '\0') return 0;

    /* 3) Guardar en el arreglo */
    Actividad *a = &actividades[total];
    snprintf(a->id, MAX_ID, "%s", id);
    snprintf(a->nombre, MAX_NOMBRE, "%s", nombre);

    if (tiempo[0] == '\0') {
        a->tiempo_ms = 100 + rand() % 4901;   /* aleatorio entre 100 y 5000 */
    } else {
        a->tiempo_ms = atoi(tiempo);
    }

    /* 4) Separar las dependencias por coma */
    a->num_deps = 0;
    char *dep = strtok(campo_deps, ",");
    while (dep != NULL && a->num_deps < MAX_DEPS) {
        dep = quitar_espacios(dep);
        if (dep[0] != '\0') {
            snprintf(a->deps[a->num_deps], MAX_ID, "%s", dep);
            a->num_deps++;
        }
        dep = strtok(NULL, ",");
    }
    return 1;
}

/* Devuelve la posicion de la actividad con ese ID, o -1 si no existe*/

int buscar_indice (char *id) {
    for (int i = 0; i < total; i++) {
        if (strcmp(actividades[i].id, id) == 0) {
            return i;
        }
    }

    return -1;
}

int construir_grafo(void) {
    /* Revisar que no haya IDs repetidos*/
    for (int i = 0; i < total; i++) {
        for (int j = 0; j < i; j++) {
            if (strcmp(actividades[i].id , actividades[j].id) == 0) {
                fprintf(stderr, "Error: el ID '%s' esta repetido\n", actividades[i].id);
                return 0;
            }
        }
    }

    /* Convertir dependencias de textos a indices y armar los sucesores*/
    for (int i = 0; i < total; i++) {
        Actividad *a = &actividades[i];
        a->pendientes = a->num_deps;
        for (int j = 0; j < MAX_DEPS; j++) {
            a->pipes_in[j] = -1;
        }

        for (int j = 0; j < a->num_deps; j++) {
            int k = buscar_indice(a->deps[j]);
            if (k==-1) {
                fprintf(stderr, "Error: '%s' depende de '%s', que no existe\n",
                        a->id, a->deps[j]);
                return 0;
            }
            a->dep_idx[j] = k;

            /* la actividad tiene un nuevo sucesor, la actividad i*/
            Actividad *dep = &actividades[k];
            dep->sucesores = realloc(dep->sucesores, (dep->num_sucesores +1) * sizeof(int));
            if (dep->sucesores == NULL){
                perror("realloc");
                return 0;
            }
            dep->sucesores[dep->num_sucesores] = i;
            dep->num_sucesores++;
        }
    }

    return 1;
}

/* Devuelve 1 si el grafo tiene un ciclo, 0 si no. Simula la ejecucion.*/
int hay_ciclo(void) {
    int pendientes[MAX_ACT];
    int cola[MAX_ACT];
    int inicio = 0, fin = 0;

    for (int i = 0; i < total; i ++){
        pendientes[i] = actividades[i].pendientes;
        if (pendientes[i] == 0) {
            cola[fin] = i;
            fin++;
        }
    }

    while (inicio < fin) {
        int u = cola[inicio];
        inicio++;
        for (int s = 0; s < actividades[u].num_sucesores; s++) {   /* <-- CORREGIDO: faltaba "s <" */
            int v = actividades[u].sucesores[s];
            pendientes[v]--;
            if (pendientes[v] == 0) {
                cola[fin] = v;
                fin++;
            }
        }
    }
    return fin != total; /* Si no se procesaron todas hay ciclo*/
}

/* Ejecuta la tarea en un hijo: lee insumos de dependencias, simula duracion y avisa a sucesores */
void ejecutar_actividad(Actividad *a, int *pipes_salida) {
    /* Leer los insumos que mandaron las actividades de las que dependemos */
    for (int d = 0; d < a->num_deps; d++) {
        if (a->pipes_in[d] != -1) {
            char insumo[128];
            int leidos = read(a->pipes_in[d], insumo, sizeof(insumo) - 1);
            if (leidos > 0) {
                insumo[leidos] = '\0';
                printf("[PID %d] [%s] Recibio: %s\n", getpid(), a->id, insumo);
            }
            close(a->pipes_in[d]);
        }
    }

    printf("[PID %d] [INICIO] Actividad '%s' (%s) iniciada - duracion: %d ms\n",
           getpid(), a->id, a->nombre, a->tiempo_ms);
    usleep((useconds_t)a->tiempo_ms * 1000);
    printf("[PID %d] [FIN] Actividad '%s' (%s) finalizada\n",
           getpid(), a->id, a->nombre);

    /* Enviar mensaje de insumo a cada actividad dependiente por su pipe */
    char mensaje[128];
    snprintf(mensaje, sizeof(mensaje), "insumo de '%s' listo", a->nombre);
    for (int s = 0; s < a->num_sucesores; s++) {
        write(pipes_salida[s], mensaje, strlen(mensaje) + 1);
        close(pipes_salida[s]);
    }

    exit(0);
}

/* Ejecuta el plan completo respetando el límite K y el orden de dependencias. */
void ejecutar_planificador(int K) {
    int terminadas = 0;

    /* Encolar las actividades que ya están listas desde el inicio */
    for (int i = 0; i < total; i++) {
        if (actividades[i].pendientes == 0) {
            cola_listas[atras] = i;
            atras++;
        }
    }

    while (terminadas < total) {
        /* Lanzar todas las que se pueda, sin pasar de K procesos vivos */
        while (frente < atras && activos < K) {
            int i = cola_listas[frente];
            frente++;

            /* Crear pipes para pasar insumos a los sucesores */
            int *pipes_salida = NULL;
            if (actividades[i].num_sucesores > 0) {
                pipes_salida = malloc(actividades[i].num_sucesores * sizeof(int));
                if (pipes_salida == NULL) {
                    perror("malloc");
                    exit(1);
                }
            }
            for (int s = 0; s < actividades[i].num_sucesores; s++) {
                int p[2];
                if (pipe(p) < 0) {
                    perror("pipe");
                    exit(1);
                }
                pipes_salida[s] = p[1]; /* el hijo i escribe por aqui */

                /* Guardar el extremo de lectura en la dependencia correspondiente del sucesor */
                int v = actividades[i].sucesores[s];
                for (int d = 0; d < actividades[v].num_deps; d++) {
                    if (actividades[v].dep_idx[d] == i) {
                        actividades[v].pipes_in[d] = p[0];
                        break;
                    }
                }
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("Error en fork");
                exit(1);
            } else if (pid == 0) {
                ejecutar_actividad(&actividades[i], pipes_salida);
                /* nunca llega aquí: ejecutar_actividad termina con exit() */
            }

            /* El padre cierra los extremos de escritura porque solo los usa el hijo */
            for (int s = 0; s < actividades[i].num_sucesores; s++) {
                close(pipes_salida[s]);
            }
            free(pipes_salida);

            /* El padre tambien cierra los extremos de lectura que el hijo ya uso */
            for (int d = 0; d < actividades[i].num_deps; d++) {
                if (actividades[i].pipes_in[d] != -1) {
                    close(actividades[i].pipes_in[d]);
                    actividades[i].pipes_in[d] = -1;
                }
            }

            /* Soy el padre: registro este hijo como "corriendo" */
            pids_corriendo[activos] = pid;
            idx_corriendo[activos] = i;
            activos++;
        }

        if (activos == 0) {
            /* No hay nada corriendo y no hay nada más para lanzar,
               pero faltan actividades por terminar. No debería pasar
               si el DAG es válido y K >= 1. */
            fprintf(stderr, "Error interno: el planificador se quedó sin trabajo\n");
            break;
        }

        /* Esperar, BLOQUEADO (sin busy-waiting), a que CUALQUIER hijo termine */
        int estado;
        pid_t pid_terminado = wait(&estado);
        if (pid_terminado < 0) {
            perror("wait");
            break;
        }
        terminadas++;

        /* Buscar cuál de los "activos" corresponde a ese PID */
        int slot = -1;
        for (int s = 0; s < activos; s++) {
            if (pids_corriendo[s] == pid_terminado) {
                slot = s;
                break;
            }
        }

        int idx_terminada = idx_corriendo[slot];

        /* Sacarlo de la lista de corriendo (swap-remove) */
        activos--;
        pids_corriendo[slot] = pids_corriendo[activos];
        idx_corriendo[slot]  = idx_corriendo[activos];

        /* Avisar a sus sucesores que una dependencia más se cumplió */
        Actividad *term = &actividades[idx_terminada];
        for (int s = 0; s < term->num_sucesores; s++) {
            int v = term->sucesores[s];
            actividades[v].pendientes--;
            if (actividades[v].pendientes == 0) {
                cola_listas[atras] = v;
                atras++;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s plan.txt K\n", argv[0]);
        return 1;
    }
    srand(time(NULL));

    FILE *archivo = fopen(argv[1], "r");
    if (archivo == NULL) {
        perror("No se pudo abrir el archivo");
        return 1;
    }

    char linea[MAX_LINEA];
    while (fgets(linea, MAX_LINEA, archivo) != NULL) {
        if (total >= MAX_ACT) {
            fprintf(stderr, "Demasiadas actividades (máximo %d)\n", MAX_ACT);
            break;
        }
        if (quitar_espacios(linea)[0] == '\0') continue;   /* línea en blanco */

        if (leer_linea(linea)) {
            total++;
        } else {
            fprintf(stderr, "Línea inválida ignorada\n");
        }
    }
    fclose(archivo);

    if (!construir_grafo()) {
        return 1;
    }
    if (hay_ciclo()) {
        fprintf(stderr, "Error: el plan tiene un ciclo de dependencias\n");
        return 1;
    }

    /* Mostrar el grafo, para comprobar que quedó bien */
    for (int i = 0; i < total; i++) {
        Actividad *a = &actividades[i];
        printf("[%s] %s | pendientes=%d | sucesores:", a->id, a->nombre, a->pendientes);
        if (a->num_sucesores == 0) {
            printf(" (ninguno)");
        }
        for (int s = 0; s < a->num_sucesores; s++) {
            printf(" %s", actividades[a->sucesores[s]].id);
        }
        printf("\n");
    }

    printf("Listas para ejecutar al inicio:");
    for (int i = 0; i < total; i++) {
        if (actividades[i].pendientes == 0) {
            printf(" %s", actividades[i].id);
        }
    }
    printf("\n");

    int K = atoi(argv[2]);
    if (K < 1) {
        fprintf(stderr, "K debe ser al menos 1\n");
        return 1;
    }

    printf("\n=== Ejecutando plan con K=%d ===\n", K);
    ejecutar_planificador(K);
    printf("=== Plan completo ===\n");

    return 0;
}