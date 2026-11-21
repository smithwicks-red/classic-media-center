"""HOST TEST ONLY. Rebuilds a synthetic /metadata/JimmyAndTheFederationGang
of the real item's shape from the compact projection jimmy_files.txt
(captured 14 Nov 2026: 425 files = 30 h.264 .mp4 + 30 Cinepack .avi
+ 30 Ogg .ogv + 30 GIF + 300 thumbnails + torrent, tile, 3 metadata;
real JSON 139,112 bytes, top keys alternate_locations created d1 dir
files files_count item_last_updated item_size metadata reviews server
simplelists solo uniq workable_servers).
usage: gen_meta_jimmy.py PROJECTION OUT.json [pad_to_bytes] [server dir]
Shuffled file order (the app must sort), deterministic."""
import sys, json, hashlib, random
proj, out = sys.argv[1], sys.argv[2]
pad = int(sys.argv[3]) if len(sys.argv) > 3 else 0
server = sys.argv[4] if len(sys.argv) > 4 else 'ia800300.us.archive.org'
dirn = sys.argv[5] if len(sys.argv) > 5 else '/1/items/JimmyAndTheFederationGang'
H = lambda s, n: hashlib.sha1(s.encode()).hexdigest()[:n]
files = []
for line in open(proj):
    name, fmt, size, title, src, w, h = line.rstrip('\n').split('|')
    base = name[:-4]
    orig = base + '.avi'
    files.append({'name': orig, 'source': 'original', 'format': 'Cinepack', 'length': '3000.00',
                  'height': h, 'width': w, 'title': title, 'mtime': '1279000000',
                  'size': str(int(size) * 3), 'md5': H(orig, 32), 'crc32': H(orig + 'c', 8), 'sha1': H(orig + 's', 40)})
    for ext, f2 in (('.mp4', fmt), ('.ogv', 'Ogg Video')):
        files.append({'name': base + ext, 'source': 'derivative', 'title': title, 'format': f2, 'original': orig,
                      'mtime': '1279100000', 'size': size if ext == '.mp4' else str(int(size) // 2),
                      'md5': H(base + ext, 32), 'crc32': H(base + ext + 'c', 8), 'sha1': H(base + ext + 's', 40),
                      'length': '3000.03', 'height': h, 'width': w})
    files.append({'name': base + '.gif', 'source': 'derivative', 'format': 'Animated GIF', 'original': orig,
                  'mtime': '1279100000', 'size': '300000', 'md5': H(base + 'g', 32), 'crc32': H(base + 'gc', 8), 'sha1': H(base + 'gs', 40)})
    for t in range(10):
        n = '%s.thumbs/%s_%06d.jpg' % (base, base, t * 300 + 1)
        files.append({'name': n, 'source': 'derivative', 'format': 'Thumbnail', 'original': orig, 'mtime': '1279100000',
                      'size': '5000', 'md5': H(n, 32), 'crc32': H(n + 'c', 8), 'sha1': H(n + 's', 40)})
files += [{'name': 'JimmyAndTheFederationGang_archive.torrent', 'source': 'metadata', 'btih': H('bt', 40), 'mtime': '1', 'size': '90000', 'md5': H('t', 32), 'crc32': 'abcd1234', 'sha1': H('ts', 40), 'format': 'Archive BitTorrent'},
          {'name': '__ia_thumb.jpg', 'source': 'original', 'mtime': '1', 'size': '9000', 'md5': H('i', 32), 'crc32': '12345678', 'sha1': H('is', 40), 'format': 'Item Tile', 'rotation': '0'}]
for n in ('JimmyAndTheFederationGang_meta.xml', 'JimmyAndTheFederationGang_files.xml', 'JimmyAndTheFederationGang_meta.sqlite'):
    files.append({'name': n, 'source': 'original', 'format': 'Metadata', 'md5': H(n, 32), 'summation': 'md5'})
random.Random(20261114).shuffle(files)
k = 0
while pad and len(json.dumps(files)) < pad - 3000:      # bigger variant: more thumbnails
    n = 'extra.thumbs/extra_%06d.jpg' % k; k += 1
    files.append({'name': n, 'source': 'derivative', 'format': 'Thumbnail', 'original': 'x.avi', 'mtime': '1279100000',
                  'size': '5000', 'md5': H(n, 32), 'crc32': H(n + 'c', 8), 'sha1': H(n + 's', 40)})
doc = {'alternate_locations': {'servers': [{'server': server, 'dir': dirn}], 'workable': [{'server': server, 'dir': dirn}]},
       'created': 1763100000, 'd1': server, 'd2': server.replace('ia8', 'ia6').replace('.us.', '.ca.'), 'dir': dirn,
       'files': files, 'files_count': len(files), 'item_last_updated': 1700000000, 'item_size': 30000000000,
       'metadata': {'identifier': 'JimmyAndTheFederationGang', 'mediatype': 'movies', 'collection': ['opensource_movies'],
                    'title': 'Jimmy and the Federation Gang', 'description': '<p>Season 1 — 30 episodes. Café “quotes”.</p>'},
       'reviews': [], 'server': server, 'simplelists': {}, 'uniq': 1234, 'workable_servers': [server]}
open(out, 'w').write(json.dumps(doc))
