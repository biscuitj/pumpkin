# Pixel companion configuration API

Use `GET /api/config` to read device settings and `POST /api/config` to update them. Send the fields you intend to change.

| Field | Values | Default |
| --- | --- | --- |
| `ambient_enabled` | Boolean; includes the companion display in navigation | `false` |
| `ambient_character` | `0` Pip (goldfish), `1` Boo (ghost), `2` Mochi (axolotl), `3` Sprout (dinosaur), `4` Pebble (turtle), `5` Inky (octopus), `6` Maple (red panda), `7` Patch (pumpkin), `8` Toffee (cat), `9` Kiko (frog), `10` Domino (penguin), `11` Hoot (owl), `12` Leo (lion), `13` Stella (unicorn), `14` Kobi (crab) | `7` on a new clock, `0` after an upgrade |
| `ambient_style` | `0` companion + text, `1` companion + range icon, `2` centered companion only | `0` |
| `ambient_use_glucose_colors` | Boolean; uses `color_low`, `color_in_range`, and `color_high` for status text/icons | `false` |
| `ambient_seasonal` | Boolean; enables seasonal surprises in companion-only style while in range | `true` |

For example, select Maple with text and the owner's glucose colors:

```json
{
  "ambient_character": 6,
  "ambient_style": 0,
  "ambient_use_glucose_colors": true
}
```

## Deprecated field: `ambient_creature`

`ambient_creature` is retained only for compatibility with older fish/ghost clients. New clients must use `ambient_character`.

- GET returns `1` for Boo and `0` for every other companion. This is a lossy compatibility value, not the selected companion's ID.
- A write containing only `ambient_creature` selects Pip (`0`) or Boo (`1`). An older client that reads settings and writes this field back can unintentionally replace Mochi, Sprout, Pebble, Inky, Maple, Patch or one of the newer pets with Pip.
- When both fields are supplied, `ambient_character` takes precedence.

To migrate, read and write `ambient_character` and omit `ambient_creature` from updates. The legacy field may be removed in a future release; no removal version is scheduled. It remains functional in this release so existing clients can migrate.
