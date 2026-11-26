#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
pack_release_catalog.py: Сборка облегчённого релизного каталога PS4 Storre.
Читает полную мастер-базу (tools/master_catalog/catalog.{json,pak}),
оставляет ТОЛЬКО игры с реальными раздачами (непустой 'th'),
перепаковывает их обложки ('c') и описания ('d') без неиспользуемых блоков
в assets/misc/catalog.{json,pak} и синхронизирует их в build_win/assets/misc/.
"""

import json
import os
import shutil
import sys
import time


def pack_release(master_dir: str, release_misc_dir: str, win_misc_dir: str | None = None) -> None:
    t0 = time.time()
    m_json = os.path.join(master_dir, "catalog.json")
    m_pak = os.path.join(master_dir, "catalog.pak")

    if not os.path.exists(m_json) or not os.path.exists(m_pak):
        raise FileNotFoundError(f"Мастер-база не найдена в {master_dir}")

    os.makedirs(release_misc_dir, exist_ok=True)
    r_json = os.path.join(release_misc_dir, "catalog.json")
    r_pak = os.path.join(release_misc_dir, "catalog.pak")
    r_json_tmp = r_json + ".tmp"
    r_pak_tmp = r_pak + ".tmp"

    print(f"=== Сборка облегчённого релизного каталога ===")
    print(f"Мастер-база:  {master_dir}")
    print(f"Релиз-папка:  {release_misc_dir}")

    with open(m_json, "r", encoding="utf-8") as f:
        data = json.load(f)

    all_games = data.get("games", [])
    with_torrent = [g for g in all_games if g.get("th")]
    if with_torrent:
        selected = with_torrent
    else:
        selected = all_games

    print(f"Игр в мастер-базе: {len(all_games):,} -> отобрано с раздачами (th): {len(selected):,}")

    with open(m_pak, "rb") as pak_in:
        header = pak_in.read(16)
        if not header.startswith(b"PS4CATPK"):
            raise RuntimeError(f"Некорректный заголовок мастер-пака: {header!r}")

        with open(r_pak_tmp, "wb") as pak_out:
            pak_out.write(header)
            write_offset = 16
            covers_ok = 0
            covers_bad = 0
            descs_ok = 0

            release_games = []
            for g in selected:
                rg = dict(g)
                rg.pop("s", None)

                c = g.get("c")
                if c and len(c) >= 2 and c[1] > 0:
                    old_off, length = int(c[0]), int(c[1])
                    pak_in.seek(old_off)
                    blob = pak_in.read(length)
                    if len(blob) != length:
                        raise RuntimeError(f"Битый блок обложки у {g.get('id')}: ждал {length}, прочтено {len(blob)}")
                    is_img = blob.startswith(b"\xff\xd8") or blob.startswith(b"\x89PNG") or blob.startswith(b"RIFF")
                    if is_img:
                        pak_out.write(blob)
                        rg["c"] = [write_offset, length]
                        write_offset += length
                        covers_ok += 1
                    else:
                        rg.pop("c", None)
                        covers_bad += 1

                d = g.get("d")
                if d and len(d) >= 2 and d[1] > 0:
                    old_off, length = int(d[0]), int(d[1])
                    pak_in.seek(old_off)
                    blob = pak_in.read(length)
                    if len(blob) != length:
                        raise RuntimeError(f"Битый блок описания у {g.get('id')}: ждал {length}, прочтено {len(blob)}")
                    pak_out.write(blob)
                    rg["d"] = [write_offset, length]
                    write_offset += length
                    descs_ok += 1

                release_games.append(rg)

    # Верификация заголовков всех упакованных обложек
    with open(r_pak_tmp, "rb") as vf:
        for rg in release_games:
            c = rg.get("c")
            if c and len(c) >= 2 and c[1] > 0:
                vf.seek(c[0])
                magic = vf.read(4)
                if not (magic.startswith(b"\xff\xd8") or magic.startswith(b"\x89PNG") or magic.startswith(b"RIFF")):
                    raise RuntimeError(f"Повреждённая обложка у {rg.get('id')}: magic={magic!r}")

    new_pak_size = os.path.getsize(r_pak_tmp)
    release_doc = {
        "version": data.get("version", 1),
        "count": len(release_games),
        "base_pak": new_pak_size,
        "games": release_games,
    }
    with open(r_json_tmp, "w", encoding="utf-8") as f:
        json.dump(release_doc, f, ensure_ascii=False, separators=(",", ":"))

    os.replace(r_pak_tmp, r_pak)
    os.replace(r_json_tmp, r_json)

    # Синхронизация в build_win/assets/misc (и очистка лишних тяжёлых бэкапов в build_win)
    if win_misc_dir and os.path.exists(os.path.dirname(win_misc_dir)):
        os.makedirs(win_misc_dir, exist_ok=True)
        shutil.copy2(r_json, os.path.join(win_misc_dir, "catalog.json"))
        shutil.copy2(r_pak, os.path.join(win_misc_dir, "catalog.pak"))
        for stale in ("catalog.pak.full_backup", "catalog.json.full_backup", "catalog.json.bak", "near_matches.csv"):
            sp = os.path.join(win_misc_dir, stale)
            if os.path.exists(sp):
                os.remove(sp)
        stale_cache = os.path.join(win_misc_dir, ".media_cache")
        if os.path.exists(stale_cache):
            shutil.rmtree(stale_cache, ignore_errors=True)

    master_pak_mb = os.path.getsize(m_pak) / 1048576.0
    rel_pak_mb = os.path.getsize(r_pak) / 1048576.0
    rel_json_kb = os.path.getsize(r_json) / 1024.0
    print(f"Упаковано обложек: {covers_ok:,} (отброшено HTML/битых: {covers_bad}), описаний: {descs_ok:,} за {time.time() - t0:.2f} сек.")
    print(f"Мастер catalog.pak: {master_pak_mb:.2f} МБ -> Релизный catalog.pak: {rel_pak_mb:.2f} МБ")
    print(f"Релизный catalog.json: {rel_json_kb:.1f} КБ ({len(release_games):,} игр)")


def main() -> int:
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(tools_dir)
    master_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(tools_dir, "master_catalog")
    release_misc_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(project_dir, "assets", "misc")
    win_misc_dir = os.path.join(project_dir, "build_win", "assets", "misc")
    pack_release(master_dir, release_misc_dir, win_misc_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
