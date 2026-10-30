# Flujo de trabajo del repositorio

Por instrucción del usuario, al completar cada cambio solicitado:

1. Ejecutar las comprobaciones pertinentes y revisar el diff.
2. Crear un commit que describa el cambio completo.
3. Hacer push al remoto configurado, sin pedir de nuevo confirmación.

Integrar los avances remotos antes del push. No usar force-push para sobrescribir
historia compartida. Mantener fuera del control de versiones los directorios
generados `out/`, `build/`, `entrega/` y las cachés de Python.
