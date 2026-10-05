# msgold

Visor del precio del oro con el indicador **Parabolic SAR**, escrito en C++17
con SDL3 y Dear ImGui (incluido en `src/vendor/imgui`, backends SDL3 +
SDL_Renderer). En Windows usa la fuente Segoe UI; en Linux, DejaVu si está
instalada (si no, la fuente integrada de ImGui).

## Funcionalidades

- **Periodos**: últimos 10 años, 5 años, 1 año, 6 meses, 3 meses y 1 mes
  (botones o teclas `1`-`6`).
- **Gráfico** de velas o de línea de cierre (en *Auto* se usan velas cuando
  hay sitio, al menos 4 px por barra), con el eje de precios a la derecha,
  la etiqueta del último cierre y la del SAR actual.
- **Parabolic SAR** (Wilder): puntos verdes bajo el precio en tendencia
  alcista y rojos por encima en tendencia bajista; un triángulo marca cada
  cambio de tendencia. Paso y máximo de aceleración editables (por defecto
  0,02 / 0,20; *Reset* los restaura). El SAR se calcula sobre todo el
  histórico descargado, así que en el borde izquierdo del periodo ya está
  estabilizado, igual que en una plataforma de trading.
- **Velas**: *Auto* (semanales para 5 y 10 años, diarias para el resto),
  diarias, semanales o mensuales. Las semanales/mensuales se agregan
  localmente a partir de los datos diarios.
- **Panel lateral**: último cierre y variación diaria, variación, máximo y
  mínimo del periodo, tendencia actual del SAR (valor, distancia al precio,
  fecha del último giro) y la lista de señales del periodo con el resultado
  de cada una hasta la siguiente señal (o hasta hoy).
- **Detalle al pasar el ratón**: cruceta con apertura, máximo, mínimo,
  cierre, variación, SAR y tendencia de la barra.
- **USD o EUR** (botones de la cabecera o tecla `E`): en EUR cada vela se
  convierte con el cierre EUR/USD de ese día (o el último anterior si el
  mercado de divisas no cotizó), y el SAR se calcula sobre la serie en
  euros, así que sus señales pueden diferir de las de USD. El panel muestra
  el tipo EUR/USD y el precio en la otra moneda.
- **Zoom**: la rueda del ratón acerca/aleja alrededor de la barra bajo el
  cursor (mínimo 10 barras, máximo todo el histórico descargado); arrastrar
  desplaza la vista en el tiempo; doble clic o `Esc` vuelve al periodo
  completo, igual que elegir otro periodo. Con zoom, las estadísticas y las
  señales del panel se refieren a la ventana visible, y la vista se recuerda
  por fechas, así que sobrevive a las actualizaciones automáticas.

## Fuente de datos

Los datos vienen del endpoint público de gráficos de **Yahoo Finance**
(`query1.finance.yahoo.com/v8/finance/chart`), símbolo `GC=F` (futuros del
oro COMEX, USD por onza troy, el precio de referencia más usado). Se
descargan 11 años de velas diarias OHLC (10 años visibles + 1 de
calentamiento del SAR), y también el histórico de `EURUSD=X` para la vista en
euros.

Se descartó investing.com porque está protegido por Cloudflare y responde
`403` a cualquier petición que no venga de un navegador real (tanto a la web
como a su API interna); tampoco ofrece una API pública. Stooq también exige
ahora JavaScript para descargar sus CSV.

La descarga usa el `curl` del sistema (incluido en Windows 10/11) en un hilo
aparte, así que la interfaz no se bloquea. En Windows el proceso se lanza sin
ventana de consola. El endpoint de Yahoo no es oficial y podría cambiar.

- Al arrancar se cargan las cachés locales `msgold.csv` y `msgold_eurusd.csv`
  (si existen) y se lanza una descarga en segundo plano. Si falla solo la del
  EUR/USD, se conserva el tipo de cambio de la caché.
- *Actualizar* (o `R`) vuelve a descargar; además se refresca solo cada
  15 minutos mientras la aplicación está abierta.

## Compilación

Con el preset de MinGW + vcpkg (SDL3 instalado en vcpkg con el triplet
`x64-mingw-dynamic`):

```sh
cmake --preset mingw-vcpkg
cmake --build build -j
./build/msgold
```

En Linux (SDL3 localizable con pkg-config):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/msgold
```

Tests unitarios de la capa de dominio (SAR, parseo de Yahoo, agregación
semanal/mensual, conversión a EUR, fechas, caché CSV; sin SDL ni ImGui):

```sh
ctest --test-dir build --output-on-failure
```

## Configuración

Un `config.ini` opcional en el directorio de trabajo:

```ini
# carpeta donde se guardan las cachés (por defecto, el directorio de trabajo)
data_root = ~/Documents
# símbolo de Yahoo Finance a representar (por defecto GC=F)
symbol = GC=F
# moneda inicial: USD o EUR (por defecto USD)
currency = EUR
# minutos entre descargas automáticas; 0 las desactiva (por defecto 15)
refresh_minutes = 15
```

Las líneas que empiezan por `#` o `;` son comentarios; `~/` se expande a
`$HOME` (o `%USERPROFILE%` en Windows). `data_root` es una carpeta que debe
existir; en Windows valen `C:\datos` y `C:/datos`.

## Estructura

```
src/gold.h/.cpp   dominio: velas, descarga de Yahoo, caché CSV, agregación,
                  conversión a EUR, SAR
src/ui.h/.cpp     estado de la app, cabecera, controles, panel lateral, estilo
src/chart.cpp     gráfico de precio + SAR (ImDrawList)
src/main.cpp      config.ini, inicialización de SDL y bucle de eventos
tests/            tests de la capa de dominio
```

`msgold.csv` y `msgold_eurusd.csv` son texto plano:
`date,open,high,low,close`, una línea por sesión.

Para capturas sin interacción: `MSGOLD_SHOT=captura.png` guarda una captura y
sale; `MSGOLD_PERIOD=0..5` elige el periodo, `MSGOLD_MOUSE=x,y` simula el
ratón (para ver el tooltip), `MSGOLD_WHEEL=n` gira la rueda `n` pasos en esa
posición, `MSGOLD_EUR=1` muestra euros y `MSGOLD_ZOOM=AAAA-MM-DD,AAAA-MM-DD`
fija una ventana de zoom.
