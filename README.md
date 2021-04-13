LED REST service
====

A simple service in C for controlling an RGB LED strip connected to a BitWizard Motor board over SPI or an P9813 IC, made for running on a Raspberry Pi

Supports both REST and MQTT interfaces for status and modification.

HTTP endpoints;
- `GET /light/state`
- `POST /light/state` - Using either query or form-encoded `state=0|off|1|on`
- `DELETE /light/state`
- `GET /light/rgb`
- `POST /light/rgb` - Using either query or form-encoded `r=0..255` `g=0..255` `b=0..255`
- `DELETE /light/rgb`
- `GET /light/hsv`
- `POST /light/hsv` - Using either query or form-encoded `h=0..360` `s=0..1` `v=0..1`
- `DELETE /light/hsv`
- `GET /light/temperature` (for blackbody radiation, in Kelvin)
- `POST /light/temperature` - Using either query or form-encoded `k=1000..40000` `v=0..1` 
- `DELETE /light/temperature`

Published MQTT topics; (Using the default prefix of `light`)
- `light/state` - `on`|`off`
- `light/temperature` - blackbody radiation in Kelvin (`1000..40000`)
- `light/rgb` - comma-separated RGB color (`0..255`)
- `light/color` - comma-separated hue (`0..360`) and saturation (`0..100`)
- `light/brightness` - brightness (`0..255`)

Subscribed MQTT topics; (Using the default prefix of `light`)
- `light/state/set` - Accepts `on`|`off`
- `light/temperature/set` - Accepts Kelvin `1000..40000`
- `light/color/set` - Accepts comma-separated hue and saturation in `0..360` and `0..100`
- `light/brightness/set` - Accepts brightness in `0..100`
- `light/rgb/set` - Accepts comma-separated RGB in `0..255` (Auto-scales to brightness)
