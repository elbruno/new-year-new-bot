Import("env")

images = env.get("FLASH_EXTRA_IMAGES", [])
env.Replace(
    FLASH_EXTRA_IMAGES=[
        ("0xd000" if str(offset).lower() == "0xe000" and str(path).endswith("boot_app0.bin") else offset, path)
        for offset, path in images
    ]
)
