// Schrift → 4-Bit-Alpha-Bitmaps als C-Header (CoreText, kantengeglättet).
// swiftc -O tools/mkfont.swift -o tools/mkfont
// tools/mkfont <font.woff2|ttf> <px> <fett> <name> <zeichen|ascii> <out.h> [vorschau.png]
// fett: Kontur in Pixeln, die zusätzlich um jedes Zeichen gezogen wird (0 = Originalschnitt)
import CoreGraphics
import CoreText
import Foundation
import ImageIO

let a = CommandLine.arguments
guard a.count >= 7 else { print("Aufruf: mkfont font px gewicht name zeichen out.h [vorschau.png]"); exit(1) }
let px = CGFloat(Double(a[2])!), embolden = CGFloat(Double(a[3])!), name = a[4]
var chars: [UnicodeScalar] = []
if a[5] == "ascii" {
  chars = (32...126).map { UnicodeScalar($0)! } + "ÄÖÜäöüß×·–„“”…°€éèáàâñçÉ".unicodeScalars
} else { chars = Array(a[5].unicodeScalars) }

let data = try! Data(contentsOf: URL(fileURLWithPath: a[1])) as CFData
let desc = CTFontManagerCreateFontDescriptorFromData(data)!
let font = CTFontCreateWithFontDescriptor(desc, px, nil)
let ascent = CTFontGetAscent(font), descent = CTFontGetDescent(font)

struct G { var cp: UInt32; var w = 0, h = 0, ox = 0, oy = 0, adv = 0; var bits: [UInt8] = [] }
var glyphs: [G] = []
for ch in chars {
  var u = Array(String(ch).utf16), gl = [CGGlyph](repeating: 0, count: u.count)
  guard CTFontGetGlyphsForCharacters(font, &u, &gl, u.count), gl[0] != 0 else { FileHandle.standardError.write("fehlt: \(ch)\n".data(using: .utf8)!); continue }
  var adv = CGSize.zero
  CTFontGetAdvancesForGlyphs(font, .horizontal, [gl[0]], &adv, 1)
  let cw = Int(px * 2) + 8, chh = Int(ascent + descent) + 8
  let ctx = CGContext(data: nil, width: cw, height: chh, bitsPerComponent: 8, bytesPerRow: cw, space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue)!
  ctx.setShouldAntialias(true); ctx.setAllowsFontSubpixelPositioning(false); ctx.setShouldSmoothFonts(false)
  ctx.setFillColor(gray: 0, alpha: 1); ctx.fill(CGRect(x: 0, y: 0, width: cw, height: chh))
  ctx.setFillColor(gray: 1, alpha: 1)
  let baseX = 4, baseY = Int(descent) + 4   // Koordinaten von unten
  if let path = CTFontCreatePathForGlyph(font, gl[0], nil) {
    ctx.translateBy(x: CGFloat(baseX), y: CGFloat(baseY))
    ctx.addPath(path)
    if embolden > 0 { ctx.setStrokeColor(gray: 1, alpha: 1); ctx.setLineWidth(embolden); ctx.setLineJoin(.round); ctx.drawPath(using: .fillStroke) }
    else { ctx.fillPath() }
  }
  let buf = ctx.data!.assumingMemoryBound(to: UInt8.self)
  var x0 = cw, y0 = chh, x1 = -1, y1 = -1
  for y in 0..<chh { for x in 0..<cw where buf[y * cw + x] > 8 { x0 = min(x0, x); x1 = max(x1, x); y0 = min(y0, y); y1 = max(y1, y) } }
  var g = G(cp: ch.value); g.adv = Int(adv.width.rounded())
  if x1 >= 0 {
    g.w = x1 - x0 + 1; g.h = y1 - y0 + 1
    g.ox = x0 - baseX
    // Zeile 0 des Puffers ist oben; Abstand der Oberkante zur Grundlinie (positiv = darüber)
    let baselineRow = chh - 1 - baseY
    g.oy = baselineRow - y0
    for y in y0...y1 {
      var row: [UInt8] = []
      for x in stride(from: x0, through: x1, by: 2) {
        let v0 = UInt8((Int(buf[y * cw + x]) + 8) / 17), v1 = x + 1 <= x1 ? UInt8((Int(buf[y * cw + x + 1]) + 8) / 17) : 0
        row.append((v0 << 4) | v1)
      }
      g.bits += row
    }
  }
  glyphs.append(g)
}
glyphs.sort { $0.cp < $1.cp }
var out = "// Erzeugt von tools/mkfont.swift, abgeleitet von \((a[1] as NSString).lastPathComponent.components(separatedBy: "-").first!) \(Int(px)) px\(embolden > 0 ? ", verstärkt \(embolden) px" : "") (SIL Open Font License 1.1, siehe licenses/OFL-1.1.txt)\n"
out += "#pragma once\n#include \"font.h\"\n\nnamespace fonts {\n\nstatic const uint8_t \(name)_bits[] = {"
var off = 0, offs: [Int] = []
for (i, g) in glyphs.enumerated() {
  offs.append(off)
  if i % 1 == 0 { out += "\n " }
  for b in g.bits { out += String(format: "0x%02X,", b) }
  off += g.bits.count
}
out += "\n};\n\nstatic const Glyph \(name)_glyphs[] = {\n"
for (i, g) in glyphs.enumerated() { out += "  {0x\(String(g.cp, radix: 16)), \(g.w), \(g.h), \(g.ox), \(g.oy), \(g.adv), \(offs[i])},\n" }
out += "};\n\nconst Font \(name) = {\(name)_glyphs, \(glyphs.count), \(name)_bits, \(Int((ascent + descent).rounded())), \(Int(ascent.rounded()))};\n\n}  // namespace fonts\n"
try! out.write(toFile: a[6], atomically: true, encoding: .utf8)
print("\(name): \(glyphs.count) Zeichen, \(off) Byte, Höhe \(Int(ascent + descent)), Oberlänge \(Int(ascent))")

if a.count > 7 {   // Vorschau: alle Zeichen in Zeilen
  let W = 760, lineH = Int(ascent + descent) + 6, perLine = 32
  let H = lineH * ((glyphs.count + perLine - 1) / perLine) + 10
  var img = [UInt8](repeating: 0, count: W * H)
  for (i, g) in glyphs.enumerated() {
    var x = 6 + (i % perLine) * Int(px * 0.9), base = 4 + (i / perLine) * lineH + Int(ascent)
    x += g.ox; base -= g.oy
    let rb = (g.w + 1) / 2
    for y in 0..<g.h { for xx in 0..<g.w {
      let b = g.bits[y * rb + xx / 2], v = xx % 2 == 0 ? b >> 4 : b & 15
      let X = x + xx, Y = base + y
      if X >= 0 && X < W && Y >= 0 && Y < H { img[Y * W + X] = max(img[Y * W + X], v * 17) }
    } }
  }
  let prov = CGDataProvider(data: Data(img) as CFData)!
  let im = CGImage(width: W, height: H, bitsPerComponent: 8, bitsPerPixel: 8, bytesPerRow: W, space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGBitmapInfo(rawValue: 0), provider: prov, decode: nil, shouldInterpolate: false, intent: .defaultIntent)!
  let dst = CGImageDestinationCreateWithURL(URL(fileURLWithPath: a[7]) as CFURL, "public.png" as CFString, 1, nil)!
  CGImageDestinationAddImage(dst, im, nil); CGImageDestinationFinalize(dst)
}
