3. La Caja Eaton-Fuller (Neumática, Rangos y Splitter)

Los camiones no tienen 6 marchas. Tienen cajas de 12, 13 o 18 marchas manejadas por aire comprimido.

    El problema actual: Hacemos state.currentGear = 1, 2, 3...

    La implementación: La caja se divide en tres secciones físicas en serie.

        MainBox (Las marchas de la palanca: 1, 2, 3, 4).

        Range (La perilla de Alta/Baja: Multiplica la reducción).

        Splitter (El botón lateral: Divide cada marcha a la mitad).

        El "Toque" Simulador: Los cambios de Range o Splitter funcionan con actuadores neumáticos. Si el jugador se quedó sin presión en los tanques de aire (airPressure < 60 PSI), la caja se queda atascada en Alta y no puede bajar a Baja. ¡Ahí tenés gameplay del bueno basado en física!

4. Cristalización y Desgaste del Embrague (Clutch Glazing)

Tenemos el daño de los engranajes, pero el embrague también sufre si sos un perro manejando.

    La implementación: En el código que armamos, ya calculamos la potencia disipada en calor (heatPower = transmittedTorque * clutchSlip).

    La física: Si esa potencia hace que el disco de embrague pase de los 250°C a 300°C, el material de fricción se cristaliza.

    Efecto: Modificamos el MAX_CLUTCH_TORQUE (que pusimos en 3500 Nm). Por cada segundo por encima de esa temperatura crítica, la capacidad máxima baja. Si te quedás atascado en una subida en 3ra patinando el embrague, lo cocinás. Tu MAX_CLUTCH_TORQUE baja a 1500 Nm y el camión ya no puede subir la cuesta porque el embrague patina permanentemente, incluso soltando el pedal.

5. Viscosidad del Aceite y Fricción Interna

El aceite frío es como miel.

    Implementación: Una función donde la resistencia interna del motor (que frena la inercia) depende de la engineTemp o oilTemp.

    A 10°C, el motor tiene que vencer 300 Nm de fricción interna solo para mantenerse prendido (por eso regula inestable en frío). A 90°C, la viscosidad cae y la fricción es de solo 50 Nm, dejando más torque libre para las ruedas y bajando el consumo de diésel.

6. Consumo Volumétrico Real (BSFC)

    Implementación: En vez de que consuma litros por kilómetro, armamos un mapa BSFC (Brake Specific Fuel Consumption). El consumo se calcula integrando los gramos de diésel inyectados por ciclo. Varía según la posición del acelerador y la eficiencia de la combustión en esas RPM exactas.

1. Sistema de Frenos Neumáticos (Air Brakes) 🛑

Los camiones no usan líquido de frenos, usan aire a presión. Esto te cambia todo el paradigma.

    La Física: Tenés que agregar un compresor de aire acoplado directamente al giro del motor (te roba un poco de HP). Ese compresor llena unos tanques hasta un corte (ej. 120 PSI o ~8 Bar).

    La Jugabilidad: Cada vez que pisás el freno, gastás aire. Si venís en bajada abusando del freno y el motor va a muy bajas RPM (el compresor gira lento y no da abasto), los tanques se vacían.

    El Castigo: Si la presión cae por debajo de 60 PSI, los "spring brakes" (frenos de resorte de emergencia) se bloquean mecánicamente. El camión se te clava en el medio de la ruta y no lo movés hasta poner el motor en neutro y acelerarlo en vacío para volver a cargar los tanques.

2. Pérdida de Tracción y Modelo de Neumáticos (Slip Ratio) 🛞

En este momento, la tracción de tu camión es "infinita". Todo el torque que sale del diferencial se convierte en avance perfecto.

    La Física: Necesitamos una curva básica de Pacejka. Hay que calcular la diferencia de velocidad entre cómo gira la rueda y cómo avanza el asfalto bajo el camión (Slip Ratio).

    La Jugabilidad: Si le metés un rebaje violento o pisás a fondo en 1ra con 1800HP y sin carga atrás, el torque tiene que superar el coeficiente de fricción estática del asfalto. La rueda empieza a patinar (quemar llanta), las RPM se disparan al corte y el camión no avanza casi nada hasta que levantes la pata.

3. Fading (Fatiga) y Desgaste Dinámico del Embrague 🔥

Ya tenés la base perfecta: estás calculando el heatPower que genera el embrague al patinar.

    La Física: Ahora mismo tu MAX_CLUTCH_TORQUE es clavado en 3500. Pero los materiales de fricción cristalizan con el calor.

    El Castigo: Si el disco de embrague pasa de cierta temperatura (ej. 250°C por querer salir en 3ra en un repecho con 40 toneladas), el coeficiente de fricción se desploma. El MAX_CLUTCH_TORQUE cae a 1000, el embrague patina sin que toques el pedal, y no te queda otra que frenar, poner punto muerto y dejarlo enfriar.

4. Elasticidad del Drivetrain (Elastocinemática / "Cardán") ⛓️

El eje de transmisión (el cardán) no es una varilla mágica e infinitamente rígida. Es un fierro larguísimo que se retuerce bajo el torque.

    La Física: Se modela como un resorte torsional (un sistema masa-resorte-amortiguador) puesto entre la salida de la caja y el diferencial.

    La Jugabilidad: Esto es lo que da ese efecto de "gomazo" o cabeceo cuando soltás el acelerador de golpe o cuando tirás un cambio brusco. Generaría pequeñas oscilaciones en las RPM y en el torque que le llega a la rueda. Te daría esa sensación de inercia viva al manejar.

5. Cargas Auxiliares Parásitas (Engine Loads) ⚙️

El torque neto que sacás hoy solo lidia con la fricción interna y el escape.

    Viscoso del Radiador: Si la engineTemp pasa los 90°C, se acopla el ventilador gigante del radiador. Eso le arranca unos 20-30HP al motor de la nada (y se escucha un zumbido bárbaro).

    Freno de Motor Realista (Exhaust Brake / Jake Brake): Ya tenés una base, pero el Jake Brake (freno de válvulas) depende fuertemente de las RPM. Si vas a 1000 RPM no frena nada; si lo metés a 2200 RPM en bajada, las válvulas abren en el punto muerto superior y el camión literalmente hace de compresor de aire masivo, frenando bestialmente.


6. Caja de cambios con problemas realistas. Rebaje agresivo frena bastante pero no necesariamente destruye la caja, vuelve a neutral. Los cambios demoran un poco en entrar y hacerse efectivos, solucionando el error del acelerar en vacío que a veces ocurre.

7. Luego de la cristalización de los frenos a 450 grados aún hay un punto de retorno, luego viene la deformación y el punto de no retorno real a los 650 grados.