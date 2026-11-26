#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
strip_screenshots.py: Очистка каталога PS4 Storre от неиспользуемых скриншотов.
Перепаковывает catalog.pak, сохраняя только обложки ("c") и описания ("d"),
удаляет списки скриншотов ("s") из catalog.json и пересчитывает точные смещения.
"""

import os
import sys
import json
import time

def main():
    start_time = time.time()
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(tools_dir)
    misc_dir = os.path.join(project_dir, "assets", "misc")

    json_path = os.path.join(misc_dir, "catalog.json")
    pak_path = os.path.join(misc_dir, "catalog.pak")

    if not os.path.exists(json_path) or not os.path.exists(pak_path):
        print(f"Error: Required files not found in {misc_dir}")
        sys.exit(1)

    print("=== Очистка каталога PS4 Storre от скриншотов ===")
    print(f"Чтение исходного catalog.json ({os.path.getsize(json_path):,} байт)...")

    with open(json_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    games = data.get("games", [])
    total_games = len(games)
    print(f"Всего игр в базе: {total_games}")

    pak_new_path = pak_path + ".clean"
    json_new_path = json_path + ".clean"

    old_pak_size = os.path.getsize(pak_path)
    print(f"Размер старого catalog.pak: {old_pak_size / (1024*1024):.2f} МБ ({old_pak_size:,} байт)")

    pak_in = open(pak_path, "rb")
    header = pak_in.read(16)
    if not header.startswith(b"PS4CATPK"):
        print(f"Warning: Unexpected header in catalog.pak: {header}")

    pak_out = open(pak_new_path, "wb")
    pak_out.write(header)
    write_offset = 16

    covers_count = 0
    descs_count = 0
    screens_removed = 0
    total_cover_bytes = 0
    total_desc_bytes = 0

    print("Перепаковка обложек и описаний...")
    for idx, g in enumerate(games):
        if idx > 0 and idx % 2000 == 0:
            print(f"  Обработано: {idx}/{total_games} игр ({write_offset / (1024*1024):.1f} МБ записано)...")

        # 1. Обложка ("c")
        c = g.get("c")
        if c and len(c) >= 2 and c[1] > 0:
            old_off, length = c[0], c[1]
            pak_in.seek(old_off)
            blob = pak_in.read(length)
            if len(blob) != length:
                raise RuntimeError(f"Failed to read cover for game {g.get('id')}: expected {length}, got {len(blob)}")
            pak_out.write(blob)
            g["c"] = [write_offset, length]
            write_offset += length
            covers_count += 1
            total_cover_bytes += length

        # 2. Описание ("d")
        d = g.get("d")
        if d and len(d) >= 2 and d[1] > 0:
            old_off, length = d[0], d[1]
            pak_in.seek(old_off)
            blob = pak_in.read(length)
            if len(blob) != length:
                raise RuntimeError(f"Failed to read desc for game {g.get('id')}: expected {length}, got {len(blob)}")
            pak_out.write(blob)
            g["d"] = [write_offset, length]
            write_offset += length
            descs_count += 1
            total_desc_bytes += length

        # 3. Удаление скриншотов ("s")
        s = g.pop("s", None)
        if s:
            screens_removed += len(s)

    pak_in.close()
    pak_out.close()

    new_pak_size = os.path.getsize(pak_new_path)
    data["base_pak"] = new_pak_size

    print(f"Сохранение очищенного catalog.json ({total_games} игр)...")
    with open(json_new_path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, separators=(",", ":"))

    new_json_size = os.path.getsize(json_new_path)
    old_json_size = os.path.getsize(json_path)

    # Верификация первых и случайных обложек
    print("Верификация целостности обложек в новом catalog.pak...")
    with open(pak_new_path, "rb") as vf:
        sample_indices = [0, 1, 5, 10, 100, 500, 1000, 5000, 10000, total_games - 1]
        for si in sample_indices:
            if si < len(games) and games[si].get("c"):
                off, length = games[si]["c"]
                vf.seek(off)
                magic = vf.read(4)
                is_valid = magic.startswith(b"\xff\xd8") or magic.startswith(b"\x89PNG") or magic.startswith(b"RIFF")
                if not is_valid:
                    raise RuntimeError(f"Corrupt image at index {si} ({games[si].get('id')}): magic {magic}")

    print("-> Верификация контрольных изображений успешно пройдена!")

    # Ротация файлов (бэкапы)
    json_bak = json_path + ".full_backup"
    pak_bak = pak_path + ".full_backup"

    if not os.path.exists(json_bak):
        os.rename(json_path, json_bak)
    else:
        os.remove(json_path)

    if not os.path.exists(pak_bak):
        os.rename(pak_path, pak_bak)
    else:
        os.remove(pak_path)

    os.rename(json_new_path, json_path)
    os.rename(pak_new_path, pak_path)

    # Синхронизация с build_win
    win_misc = os.path.join(project_dir, "build_win", "assets", "misc")
    if os.path.exists(win_misc):
        import shutil
        shutil.copy2(json_path, os.path.join(win_misc, "catalog.json"))
        shutil.copy2(pak_path, os.path.join(win_misc, "catalog.pak"))
        print(f"Каталог синхронизирован в {win_misc}")

    elapsed = time.time() - start_time
    print("=" * 60)
    print("=== Итоги очистки каталога PS4 Storre ===")
    print(f"Время выполнения:           {elapsed:.2f} сек.")
    print(f"Обложек сохранено:          {covers_count:,}")
    print(f"Описаний сохранено:         {descs_count:,}")
    print(f"Скриншотов удалено:         {screens_removed:,}")
    print(f"Размер catalog.pak (было):  {old_pak_size / (1024*1024):.2f} МБ ({old_pak_size:,} байт)")
    print(f"Размер catalog.pak (стало): {new_pak_size / (1024*1024):.2f} МБ ({new_pak_size:,} байт)")
    print(f"Экономия в catalog.pak:     -{(old_pak_size - new_pak_size) / (1024*1024):.2f} МБ (-{(old_pak_size - new_pak_size)/old_pak_size*100:.1f}%)")
    print(f"Размер catalog.json (было): {old_json_size / (1024*1024):.2f} МБ")
    print(f"Размер catalog.json (стало):{new_json_size / (1024*1024):.2f} МБ")
    print(f"Экономия в catalog.json:    -{(old_json_size - new_json_size) / (1024*1024):.2f} МБ (-{(old_json_size - new_json_size)/old_json_size*100:.1f}%)")
    print("=" * 60)

if __name__ == "__main__":
    main()
