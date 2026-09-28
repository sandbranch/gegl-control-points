# Patents checked (2026-09-28)

The status, dates and family of each patent were read on its Google
Patents page (the "status" and "anticipated expiration" fields, the
priority and filing dates, the continuation data); claim 1 was read
where noted. Not legal advice; a record of what was found, so that the
next person does not have to start over.

## Bottom line

**Every patent of Nik's U Point family is expired**, the last one in
September 2023 at the latest. All claim priority to the provisional
application 60/336,498 of 2001-10-24; the parent application 10/280,897
was filed 2002-10-24, and a US patent term runs 20 years from the
earliest non-provisional filing it claims (continuations and
continuations in part included), so the family ended 2022-10-24, apart
from patent term adjustments on two members, which ran out in 2023 (one
of them had already lapsed for fees). gimp-devtools'
docs/photoshop-gaps.md and docs/prior-art-candidates.md say "expired
2022-10-24"; that holds for the core patents, with the two exceptions
below.

## The family, as Google Patents lists it

| Patent | Title | Filed | Status on Google Patents |
|---|---|---|---|
| US 6,728,421 B2 | User definable image reference points | 2002-10-24 | Expired - Lifetime, 2022-10-24 |
| US 6,865,300 B2 | User definable image reference points (division) | 2004-04-13 | Expired - Lifetime, 2022-10-24 |
| US 7,031,547 B2 | User definable image reference points (continuation) | 2005-03-03 | Expired - Lifetime, 2022-10-24 |
| US 7,602,968 B2 | Overlaid graphical user interface and method for image processing | 2006-04-17 | Expired - Lifetime, 2023-08-16 (term adjustment) |
| US 7,602,991 B2 | User definable image reference regions (continuation in part) | 2007-08-01 | Expired - Lifetime, 2022-10-24 |
| US 8,064,725 B2 | Distortion of digital images using spatial offsets | 2009-10-10 | Expired - Fee Related, 2022-10-24 |
| US 8,625,925 B2 | Distortion of digital images using spatial offsets from image reference points | 2009-10-12 | Expired - Fee Related (would have run to 2023-09-25) |
| US 7,970,233 B2 | Distortion of digital images using spatial offsets from image reference points | 2010-08-12 | Expired - Fee Related, 2022-10-24 |
| US 8,582,877 B2 | same title | 2011-05-17 | Expired - Fee Related, 2022-11-15 |
| US 9,008,420 B2 | same title | 2013-10-09 | Expired - Lifetime, 2022-10-24 |
| US 9,471,998 B2 | same title | 2015-01-22 | Expired - Fee Related, 2022-10-24 |
| US 9,786,031 B2 | same title | 2016-10-17 | Expired - Fee Related, 2022-10-24 |
| US 10,140,682 B2 | same title | 2017-10-09 | Expired - Lifetime, 2022-10-24 |
| EP 1 449 152 B1 | User-defined image reference points | 2002-10-24 | granted 2013-10-23, Expired - Lifetime, 2022-10-24 |

Original assignee Nik Multimedia / Nik Software; current assignee Google
LLC. Inventor of the parent: Nils Kokemohr.

Claim 1 of US 6,728,421 (read): "A method for image processing of a
digital image comprising the steps of determining one or more sets of
pixel characteristics; determining for each pixel characteristic set, an
image editing function; providing a mixing function algorithm ...; and
processing the digital image by applying the mixing function algorithm
based on the one or more pixel characteristic sets and determined image
editing functions." Claim 1 of US 7,031,547 (read) is the same with
image reference points having location dependent or independent
characteristics. The description gives a "controlling function"
C_i = p_i / (p_1 + ... + p_m) that normalizes weighted inverse
differences (equation 13) and two mixing functions, one normalized and
one that adds weighted deltas to the pixel (equations 21 and 22). The
"distortion" members claim spatial offsets (warping) from reference
points, which this filter does not do.

## Other patents looked at

- Nik's "Method for dynamic range editing" (Kokemohr, priority
  2006-11-22): US 8,380,002 B2 (Expired - Fee Related), and the
  continuations US 8,737,764 B2 (active, to 2027-11-21), US 9,286,665 B2
  (active, to 2028-03-24), US 9,589,335 B2 (active, to 2027-11-21),
  US 9,934,562 B2 (Expired - Fee Related). Claim 1 of each active one was
  read: US 8,737,764 claims selectively fine-tuning the **tone mapping of
  a high dynamic range image** for a low dynamic range display from user
  input at locations; US 9,286,665 and US 9,589,335 claim **merging
  several exposures** into a high dynamic range image. This filter does
  not tone map a high dynamic range image to a lower range (its output
  range is its input range) and takes one image, so none of them reads
  on it as far as the claim text goes.
- Adobe US 10,222,962 B2, "Selective editing of images using editing
  tools with persistent tool settings" (priority 2015-11-10, active to
  2035-11-10): claim 1 read; it claims storing, per pixel and per layer,
  which tool and setting modified the pixels, and reading it back in a
  later session. This filter keeps its settings as the properties of one
  filter on a layer (GIMP's XCF does that for every filter), with no per
  pixel tool records.
- One web search for later Google filings on control points (Snapseed's
  "Selective" tool, 2013 on) found no further patent; that search was not
  exhaustive. The 2013 paper of Binder, Steiding, Wille and Kokemohr
  (README.md) describes the published method, branded U Point.
