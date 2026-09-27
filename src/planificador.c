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
} Actividad;

Actividad actividades[MAX_ACT];
int total = 0;

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

/* Simula la ejecucion de una actividad en el proceso hijo */
void ejecutar_actividad(Actividad *a) {
    printf("[PID %d] [INICIO] Actividad '%s' (%s) iniciada - duracion: %d ms\n",
           getpid(), a->id, a->nombre, a->tiempo_ms);
    /* usleep recibe microsegundos (1 ms = 1000 us) */
    usleep((useconds_t)a->tiempo_ms * 1000);
    printf("[PID %d] [FIN] Actividad '%s' (%s) finalizada\n",
           getpid(), a->id, a->nombre);
    exit(0);
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

    /* ========================================================== */
    /* PASO 1: fork() simple (Creación de procesos y espera)      */
    /* ========================================================== */
    printf("\n=== Paso 1: Prueba de fork() simple ===\n");
    for (int i = 0; i < total; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("Error en fork");
            return 1;
        } else if (pid == 0) {
            /* Proceso hijo: ejecuta la actividad y sale */
            ejecutar_actividad(&actividades[i]);
        }
    }

    /* Proceso padre: espera que todos los hijos terminen */
    for (int i = 0; i < total; i++) {
        int status;
        pid_t pid_hijo = wait(&status);
        if (pid_hijo > 0) {
            printf("[PADRE] Proceso hijo PID %d finalizo con estado %d\n",
                   pid_hijo, WEXITSTATUS(status));
        }
    }
    printf("=== Fin de prueba fork() simple ===\n");

    return 0;
}