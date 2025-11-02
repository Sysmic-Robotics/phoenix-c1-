# Phoenix-C1 — Driver modular para motor PMSM (RoboCup SSL)

Proyecto ELO301 — Sysmic Robotics  
Controlador modular para cada motor del robot, con ruta de actualización a FOC y mantenimiento sencillo en competencia. :contentReference[oaicite:0]{index=0}

---

## Estado del proyecto
- ✅ **Esquemáticos**: prácticamente completos; arquitectura modular por motor.
- 🚧 **PCB/Firmware**: siguiente hito inmediato (ver Cronograma del documento).
- 🎯 **Objetivo de costo**: ~20–30 USD/unidad (FOC-capable). :contentReference[oaicite:1]{index=1}

---

## Motivación y alcance
La placa “todo-en-uno” actual es frágil: una falla deja inoperante el sistema. Phoenix-C1 separa el control por motor para elevar confiabilidad, facilitar recambios y habilitar control avanzado (FOC) más adelante. :contentReference[oaicite:2]{index=2} :contentReference[oaicite:3]{index=3}

---

## Arquitectura del sistema (resumen)
- **MCU**: STM32G431CBU6 (control + PWM).
- **Gate driver**: DRV8323S.
- **Etapa de potencia**: 3 medios puentes con 6× MOSFET NTMFS5C612NLT1G.
- **Encoder**: AS5600 (I²C).
- **Alimentación lógica**: 3.3 V (buck).
- **Bus de potencia**: +V 10–40 V (diseño), pensado para baterías 6S/24 V del robot. :contentReference[oaicite:4]{index=4} :contentReference[oaicite:5]{index=5}

**Interfaces clave**
- I²C (AS5600), SPI (config/telemetría DRV8323S), SPI esclavo para consignas externas, PWM hacia DRV8323S, GPIO/EXTI y LED de estado. :contentReference[oaicite:6]{index=6}

---

## Firmware (V1)
- **Lazo**: control básico de velocidad con realimentación del encoder.
- **FOC**: previsto para actualización de firmware futura (hardware ya cableado). :contentReference[oaicite:7]{index=7}

**Módulos**
- Referencia/entrada (SPI esclavo), adquisición de velocidad (AS5600), generador PWM (TIMx), driver DRV8323S (config y fallas), temporización/SysTick. :contentReference[oaicite:8]{index=8}

**Flujo**
1. Init de periféricos (GPIO, TIM, I²C/SPI, PWM, DRV8323S deshabilitado).
2. Espera de referencia válida (sondeo cada 0.5 ms).
3. Lectura de velocidad (encoder).
4. Cálculo de control y actualización de duty en 3 fases.
5. Conmutación vía DRV8323S y realimentación. :contentReference[oaicite:9]{index=9}

---

## Pruebas planificadas
- **Elementos**: power-up +V∈[10,40] V, 3.3 V estable, rizado a f_PWM, separación de masas; validación SPI/I²C/PWM/GPIO. :contentReference[oaicite:10]{index=10}  
- **Funcionalidades**: arranque y control de velocidad; inyección de fallas (UVLO, nFAULT, kill-switch) y recuperación segura. :contentReference[oaicite:11]{index=11}

---

## Restricciones físicas y entorno
- **PCB** máx. 4×4 cm (montaje en el centro de motores ∅≈5 cm tipo 5010).  
- **Rango térmico** típico de competencias indoor; tolerancia a impactos moderados (carcasa plástica/impresa 3D). :contentReference[oaicite:12]{index=12}

---

## Costos (referencia)
- Meta ≈ 20–30 USD/unidad con capacidad FOC (24 drivers totales objetivo). :contentReference[oaicite:13]{index=13}  
- **BOM parcial** (ejemplo en CLP con 6 MOSFET): STM32G431, DRV8323S, NTMFS5C612NLT1G×6, AS5600, MAX15062 buck, PCB 4 capas, pasivos. :contentReference[oaicite:14]{index=14}

---


## Puesta en marcha (high-level)
1. Ensamblar y verificar 3.3 V, continuidad y ausencia de cortos.
2. Flashear firmware base (SWD/J-Link/ST-Link).
3. Probar SPI con DRV8323S (ID y registros), lectura AS5600 y PWM.  
4. Activar control de velocidad con consignas de prueba (escalón/rampa). :contentReference[oaicite:15]{index=15}

---

## Equipo
- Rodrigo Sierra, Gabriel Arcaya, Sebastián Pinochet — ELO301 (UTFSM). :contentReference[oaicite:16]{index=16}

---

## Licencia
Define la licencia aquí (MIT/BSD/GPL) según necesidades del curso/proyecto.

---

## Agradecimientos
Cátedra ELO301 y referentes tecnológicos (p. ej., drivers FOC comerciales tomados como benchmark de costo/desempeño). :contentReference[oaicite:17]{index=17}

