# Eyes and weather tile examples

Run `SYS:System/TilesExamples RAM:` on AROS to generate real `RAM:Eyes.info` and
`RAM:WeatherOffline.info` icons, using icon.library's PutDiskObject. The destination
must already exist; existing example icons are replaced. The helper also writes global
`ENV:TilesWeatherDemo`. Start the service with both definitions:

```
Run SYS:System/Tiles RAM:Eyes.info RAM:WeatherOffline.info
```

These are tile definitions, not programs to double-click. Only one Tiles service can
own the `TILES` ARexx port. If it is already running, send `TILE QUIT` to that port
using an ARexx client and wait for the port to disappear before starting this command.
Quit is deferred while external requests still await their replies; the service
retains normal activity during that wait and cannot promise a bounded shutdown when
a provider never replies.

Eyes uses `TILE=eyes`, `SIZE=2x1`, `HOVER=FAST`. Its source is internal: GET, ENV and
SET are rejected. Pointer coordinates are relative to the complete tile surface;
the renderer clamps them to that surface and keeps pupils inside their eyes. Leaving
the tile restores a neutral gaze. Pointer tracking is disabled by default. `HOVER=ON`
enables pointer events with the normal frame budget; `HOVER=FAST` explicitly requests
33 ms cadence. `HOVER=OFF` disables it. No external source or network is needed.

Weather uses `TILE=weather`, `SIZE=2x2`, `LABEL=Rome (offline demo)`,
`ENV=TilesWeatherDemo`, `EVERY=60`. The generated value `18|cloudy` is deterministic
fictional demonstration data, not a current weather observation.

For actual weather, replace LABEL with the source location and configure exactly one
source: `ENV=YourWeatherVariable` or `GET=YOURWEATHERPORT YOUR QUERY COMMAND`.
An external provider must write the ENV variable or respond through the existing
asynchronous ARexx source protocol. Tiles itself performs no HTTP requests and does
not infer a location.

The entire response is a signed integer Celsius temperature in -100..100, a single
literal `|`, then one of `sunny`, `cloudy`, `rain`, `snow`, `storm`, `fog` (case
insensitive). Examples: `-4|snow`, `+18|cloudy`, `32|sunny`. Outer spaces/tabs are
trimmed; fractions, units, internal whitespace, unknown conditions, extra fields,
control bytes and oversized responses are rejected. The parsed number holds Celsius;
text holds the normalized condition. No temperature or condition is invented after
an invalid response. `EVERY` is seconds and has a 60-second minimum.

To update the offline demonstration manually, use `SetEnv TilesWeatherDemo "-4|snow"`;
the ENV file notification (`StartNotify`) triggers a refresh. For live use, arrange for a provider to update
the configured variable and label; merely changing LABEL does not fetch that location.
