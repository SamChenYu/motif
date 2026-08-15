# Motif 
A music-native git wrapper for Muescore files 🎼


Git diffs with .mscz files are useless!
```
diff --git a/Piano Concerto in G Minor.mscz b/Piano Concerto in G Minor.mscz
index 887fb8e..c4b44bd 100644
Binary files a/Piano Concerto in G Minor.mscz and b/Piano Concerto in G Minor.mscz differ
```


Underlying Git Commands

SCORE="Piano Concerto in G Minor.mscz"
SCRATCH="/private/tmp/claude-501/-Users-samchenyu-Desktop-Projects-vibe-motif/9814323a-9ff6-4911-b80c-c18315075c4e/scratchpad"

1. Resolve HEAD's blob id for that path  (motif: blob_oid_at →
SCORE="Piano Concerto in G Minor.mscz"
git rev-parse --quiet --verify "HEAD:$SCORE"
    887fb8eeda3d31a5ca5374ea81f7a1d2fcbba294
ZERO output if it doesn't exist - then you can programmatically check for this easily

2. Fetch the "before" blob's bytes out of the object store  (motif: read_blob → git cat-file blob)
git cat-file blob "HEAD:$SCORE" > "$SCRATCH/before.mscz"
    Fetches bytes

3. The "after" is just the working-tree file
cp "$SCORE" "$SCRATCH/after.mscz"

4. Unzip each side's XML  (motif: mscz/archive → miniz)
unzip -p "$SCRATCH/before.mscz" "*.mscx" | xmllint --format - > "$SCRATCH/before.xml"
   unzip -p "$SCRATCH/after.mscz"  "*.mscx" | xmllint --format - > "$SCRATCH/after.xml"

5. Now a *text* diff works, because both sides are pretty-printed XML
   git diff --no-index "$SCRATCH/before.xml" "$SCRATCH/after.xml"
```
index e4c2536..5c5c1e3 100644
--- a/private/tmp/claude-501/-Users-samchenyu-Desktop-Projects-vibe-motif/9814323a-9ff6-4911-b80c-c18315075c4e/scratchpad/before.xml
+++ b/private/tmp/claude-501/-Users-samchenyu-Desktop-Projects-vibe-motif/9814323a-9ff6-4911-b80c-c18315075c4e/scratchpad/after.xml
@@ -11,6 +11,7 @@
     <showMargins>0</showMargins>
     <open>1</open>
     <metaTag name="arranger"/>
+    <metaTag name="audioComUrl"/>
     <metaTag name="composer"/>
     <metaTag name="copyright"/>
     <metaTag name="creationDate">2026-08-09</metaTag>
@@ -20,6 +21,7 @@
     <metaTag name="originalFormat">mid</metaTag>
     <metaTag name="platform">Apple Macintosh</metaTag>
     <metaTag name="source"/>
+    <metaTag name="sourceRevisionId"/>
     <metaTag name="translator"/>
     <metaTag name="workNumber"/>
     <metaTag name="workTitle"/>
@@ -1100,10 +1102,22 @@
             <sigN>4</sigN>
             <sigD>4</sigD>
           </TimeSig>
+          <Chord>
+            <eid>A+wG06iOjMN_7mhKj0SVR1K</eid>
+            <durationType>quarter</durationType>
+            <Note>
+              <eid>qfka9XoYzLN_nC47N0dUO8L</eid>
+              <pitch>71</pitch>
+              <tpc>19</tpc>
+            </Note>
+          </Chord>
           <Rest>
-            <eid>QrVp1Tx6ZaJ_S2RIctfF2fO</eid>
-            <durationType>measure</durationType>
-            <duration>4/4</duration>
+            <eid>qH95TvOYuCC_1W14EHVhsJM</eid>
+            <durationType>quarter</durationType>
+          </Rest>
+          <Rest>
+            <eid>KXqragJiBTG_EhribSQluFL</eid>
+            <durationType>half</durationType>
           </Rest>
         </voice>
       </Measure>
@@ -24667,10 +24681,32 @@
             <sigN>4</sigN>
             <sigD>4</sigD>
           </TimeSig>
+          <Chord>
+            <eid>0+vXX73dKgP_L2EHji+GhIB</eid>
+            <durationType>quarter</durationType>
+            <Note>
+              <eid>K/byt3ldxC_opmjdSgDUTO</eid>
+              <pitch>52</pitch>
+              <tpc>18</tpc>
+            </Note>
+          </Chord>
+          <Chord>
+            <eid>pPdf3yrPvSE_9hKH36uvx2L</eid>
+            <durationType>quarter</durationType>
+            <Note>
+              <eid>iDIxYzDjuzJ_9aqJBwo5+wL</eid>
+              <pitch>52</pitch>
+              <tpc>18</tpc>
+            </Note>
+            <Note>
+              <eid>R+1L0yWierF_jD0OeNhycRH</eid>
+              <pitch>55</pitch>
+              <tpc>15</tpc>
+            </Note>
+          </Chord>
           <Rest>
-            <eid>CZWuBQFfrvF_vHOh3JAG8BC</eid>
-            <durationType>measure</durationType>
-            <duration>4/4</duration>
+            <eid>YtNdHikx0MC_aR71uuzLhjM</eid>
+            <durationType>half</durationType>
           </Rest>
         </voice>
       </Measure>
```