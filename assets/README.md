# assets
Images used in EER's 2027 software package.

## Folders
| Name        | Info                                              |
|-------------|---------------------------------------------------|
| nosignal    | Camera no signal image                            |
| noicon      | App icon based off no signal image                |
| noicon_dark | App icon based off no singla image, but dark blue |
| trident     | App icon based off BlueStar's foam artwork        |

## Extensions
| extension | type                        |
|-----------|-----------------------------|
| .ico      | Windows icon format         |
| .icon     | Modern Apple icon format    |
| .pxd      | Pixelmator Pro Project File |

## Convert to ico
Using imagemagick:
```sh
magick IN.png -define icon:auto-resize=256,128,64,48,32,16 OUT.ico
```

## Resize for Linux

Using imagemagick:
```sh
for s in 48 128 256 512; do magick IN.png -resize ${s}x${s} -depth 8 OUT-$s.png; done
```

## Apple Icon Composer
Both the generic and Apple formatted icons are exported from Apple Icon composer, for noicon its exported without Liquid Glass enabled, and trident is exported with iOS/macOS 27 Liquid Glass enabled.