# ESPHome example

`mio-master.yaml` is a starting configuration for mapping a DI4 channel to a
binary sensor and a DO4 channel to a switch. The example uses ESPHome's
`canbus` component to carry frames; the MIO component hides the CAN identifiers
and payloads from automations.

From the repository root, generate the local external component next to the
configuration:

```sh
python3 SHARED/tools/package_esphome.py examples/esphome
```

Then validate and compile it with ESPHome from `examples/esphome/`:

```sh
esphome config mio-master.yaml
esphome compile mio-master.yaml
```

Copy `secrets.yaml.example` to `secrets.yaml` and replace the placeholders
with your local Wi-Fi credentials. Keep the real `secrets.yaml` private.

The adapter is an initial integration and still needs a successful build check
against the ESPHome release selected for the product. Review the CAN pins,
transceiver wiring, node addresses, and safety behavior for the target hardware
before connecting real loads.
