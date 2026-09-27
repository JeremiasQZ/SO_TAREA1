#!/usr/bin/env python3
import sys

def main():
    total = int(sys.argv[1]) if len(sys.argv) > 1 else 10000
    archivo = sys.argv[2] if len(sys.argv) > 2 else "plan_estres.txt"
    ancho = 20

    with open(archivo, "w") as f:
        for i in range(1, total + 1):
            if i <= ancho:
                f.write(f"{i} : act_{i} : 1 :\n")
            else:
                dep = i - ancho
                f.write(f"{i} : act_{i} : 1 : {dep}\n")

    print(f"Generado {archivo} con {total} actividades.")

if __name__ == "__main__":
    main()
