# Test photos

`fetch.py` downloads three photos from Wikimedia Commons, all CC0 (the
license was read on each photo's Commons page; the two Unsplash
uploads were published on Unsplash before 2017-06-05, when Unsplash
left CC0, as the Commons pages note). `manifest.json` has the author,
license and page of each. Commons serves standard thumbnail sizes, so
the first two arrive 3840 pixels wide; `rose-bush-24mp.jpg`
comes at its full 6000 x 4000 (24 megapixels) for the timing on a real
photo. They stay out of git to keep the repository small; `fetch.py`
makes the same set anywhere.

    tests/images/fetch.py

| File | What | Author | License |
|---|---|---|---|
| poppy-leaves.jpg | red poppies among green leaves | Spring's Flowers | CC0 |
| rose-green.jpg | a red rose before soft green leaves | Henrique Ferreira | CC0 |
| rose-bush-24mp.jpg | a red rose on a dark bush, 24 megapixels | Jeffrey Wegrzyn | CC0 |

No other photos are used by the tests, and the user's own photos never.
