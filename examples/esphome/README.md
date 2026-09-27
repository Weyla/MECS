# ESPHome example

`mecs-master.yaml` is a starting configuration for mapping a DI4 channel to a
binary sensor and a DO4 channel to a switch. The example uses ESPHome's
`canbus` component to carry frames; the MECS component hides the CAN identifiers
and payloads from automations.

The custom entity component is fetched from the GitHub repository by the
`external_components` section in the YAML. ESPHome also needs the portable C
client source files as local compile inputs, so download those from the same
repository with the packager:

```sh
curl -fsSL https://raw.githubusercontent.com/Weyla/MECS/main/SHARED/tools/package_esphome.py \
  -o /tmp/package_esphome.py
python3 /tmp/package_esphome.py --github-ref main examples/esphome
```

Then validate and compile it with ESPHome from `examples/esphome/`:

```sh
esphome config mecs-master.yaml
esphome compile mecs-master.yaml
```

Copy `secrets.yaml.example` to `secrets.yaml` and replace the placeholders
with your local Wi-Fi credentials. Keep the real `secrets.yaml` private.

`mecs-master.yaml` is the repository-hosted example:
[view it on GitHub](https://github.com/Weyla/MECS/blob/main/examples/esphome/mecs-master.yaml).
Input entities become unknown when their node stops reporting. Output switches
show off when a previously online node goes offline; the node itself returns
the physical output to inactive on heartbeat loss. The adapter is an initial
integration and still needs a successful build check against the ESPHome
release selected for the product. Review the CAN pins,
transceiver wiring, node addresses, and safety behavior for the target hardware
before connecting real loads.
