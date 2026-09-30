# Scoped final visual review — 0.6.95

Date: 2026-09-30. Read-only review of four final PNGs explicitly requested by root, using view_image to inspect the actual pixels. Two genuine .94 BEFORE PNGs were also viewed for comparison. No product/test/docs/version/Git change, GUI launch or build was performed. Only this ignored note was saved.

## Result

No blocking visual defect observed in these four captured states. This is a scoped pixel review, not all-page acceptance or proof that every field can appear simultaneously. Root reports the separate accepted native run (404 states / 83 contexts / 20 confirmations, exit 0 and empty stderr) and functional build PASS; this note does not independently rerun or replace that evidence.

## Actual images reviewed

### Decision context, wrapping and footer

| Genuine Before pixels | Final After pixels and observation |
| --- | --- |
| `Z:/CPP/ForgeMirror/build-qt/native-decision-before-0.6.94/cloud-tab-0-200.png`: 720px-wide dialog; version captions, summaries and paths show ellipses; action captions visibly cut; backup viewport shows a header with no complete snapshot row. | `Z:/CPP/ForgeMirror/build-qt/native-decision-accepted-0.6.95/cloud-tasks-200.png`: 640×520. Direction/confirmation/backup warning is readable and wraps naturally. Active Tasks tab label and comparison headers are complete. The visible local version caption is complete; summary and path use multiline text with no painted ellipsis. The lower row continues below the current scroll viewport, which is intentional scrolling, not evidence that its whole path is visible at once. Permanent footer clearly says replacement requires confirmation/backup and closing changes no files; Close is fully inside the frame with no clipped text. |
| `Z:/CPP/ForgeMirror/build-qt/native-decision-before-0.6.94/storage-200.png`: 760px-wide dialog; version, summary and path captions are ellipsized; large unused lower band. | `Z:/CPP/ForgeMirror/build-qt/native-decision-accepted-0.6.95/storage-200.png`: 640×520. Full visible warning distinguishes accepting cloud locally versus sending local to cloud, states that balance/history are not merged and original versions are backed up locally. Comparison header and visible local caption, numeric summary and path fragments are readable. Lower content remains reachable by body scroll; the screenshot alone does not show both complete versions or commit commands. Footer safety statement and Close are fully readable, with no unnecessary blank band. |

Cloud/storage initial top-of-body captures intentionally do not show the commit buttons farther down the scrollable body. No assertion about their complete glyphs or primary styling is made from these two PNGs alone. Root's actual reachability/intrinsic/native tests cover those commands. Cloud's remaining tabs scroll horizontally within the tab bar; a partially shown non-active next tab is not an outer body overflow. No new navigation is introduced.

### Error and dangerous-operation clarity

| Before evidence available in this review | Final After pixels and observation |
| --- | --- |
| No comparable shortcut error BEFORE image was reviewed; no historical defect claim. | `Z:/CPP/ForgeMirror/build-qt/native-decision-accepted-0.6.95/shortcut-200-error.png`: 640×520. Intro explains reference-only file/folder behavior. Editable single-line Name/Path fields display the text near the current caret; the leading portion is outside their internal text scroll, which is normal editing behavior and not a label clipped by layout. The permanent error shows «Файл не найден» and the complete long path, including `/missing`, across four readable lines. Add is the one visible purple primary; Cancel is secondary. Both footer commands and every visible error line fit with no overlap. Picker buttons are partly below the body viewport and require scroll, which is allowed; this PNG does not prove their full captions. |
| No comparable debit confirmation BEFORE image was reviewed; no historical defect claim. | `Z:/CPP/ForgeMirror/build-qt/native-decision-accepted-0.6.95/wallet-confirm-debit-accept-200.png`: 500×556. The actual question shows «Списать 12.50», the full long currency and profile context, balance `262.50 → 250.00`, and full long basis for the audit. Plain wrapped text stays within its column, with no cropped last line. Confirm is purple primary and Cancel has secondary treatment; both captions fit, separation is clear and there is no text/button overlap. Keyboard/default safety remains the separate runtime test, not something inferred from pixels. |

At 200% the font and action hit areas are necessarily larger. Spacing stays functional: intro/context precedes data; notice and commands remain grouped. No decorative panel or artificial breathing band is visible in the reviewed AFTER frames. Tall scrollable comparison rows are the cost of displaying long text at the requested width; they are not stretched action buttons in these captures.

## Exact final image identity

| Final image | SHA256 |
| --- | --- |
| cloud-tasks-200.png | D8D50C05EC7BD5C582143072AA7F87A880D8729D7A96BF47D7C89AB40D007228 |
| storage-200.png | 611F40175E66F4EC450923B874C770636EC0F7CFBE8CFFA6A1E44453F6D54084 |
| shortcut-200-error.png | 549AC4C7B334FDC3BFFD3A3D8727771ED5150948E56F171262221555EF549796 |
| wallet-confirm-debit-accept-200.png | 7D4989D35226A5C8885842BB3DFD4C07EA48D26D9FC591E5898865E82E33D03D |

## Limits / blocker-only conclusion

- No blocking issue found in the four requested visible states.
- Restore cell geometry, lower scroll positions, wallet maximum preview/history, shortcut Help/Menu, all other scales, focus/default/Return and domain/cancel behavior are outside this four-image pixel conclusion and use root's separate evidence.
- The global UI plan, policy-blocked 18×7 page acceptance, actual Windows motion preference approval and release/installer/user-update gates remain separate. This checkpoint does not close them.
