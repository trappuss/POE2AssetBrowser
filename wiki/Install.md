# Install

## Download and first run

1. Download the release `.zip` (or build it yourself — see [Building from source](Building-from-source)).
2. Unzip it anywhere. It is fully portable: everything it writes goes into a `data\` folder beside
   the exe, so a USB stick or a folder in Downloads is fine.
3. Run `POE2AssetBrowser.exe`.

## SmartScreen

Windows may show a "Windows protected your PC" SmartScreen prompt the first time, because the exe is
not code-signed. Click **More info ▸ Run anyway**. This is expected for any small unsigned tool.

## Point it at the game

From **File ▸ Set Path of Exile 2 folder…**, select your install — the folder that contains the
`Bundles2` directory. On Steam that is usually:

```
…\steamapps\common\Path of Exile 2
```

The first time, it reads the game's index (`Bundles2\_.index.bin`) and rebuilds all ~1.4 million
asset paths. That takes about 10–15 seconds, once — after that it loads from a cache in `data\cache\`
in well under a second, until the game patches.

## After a game patch

When the game updates, the index changes and the cache is rebuilt automatically the next time you
launch (the cache is keyed on the index file's size and timestamp). Nothing to do by hand.

See also [Troubleshooting](Troubleshooting) if it can't find the game data.
