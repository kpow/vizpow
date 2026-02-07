#!/usr/bin/env node

/**
 * Add Icon Script for vizPow
 *
 * Usage:
 *   node scripts/add-icon.js "Icon Name" '{"seg":{"i":[0,0,0,...]}}'
 *   node scripts/add-icon.js "Icon Name" path/to/icon.json
 *
 * This script will:
 *   1. Parse WLED JSON format
 *   2. Convert to CRGB array format
 *   3. Add to emoji_sprites.h
 *   4. Update ICON_COUNT, ALL_ICONS, ICON_NAMES
 *   5. Update web_server.h emojiNames array
 */

const fs = require('fs');
const path = require('path');

// Color shortcuts from emoji_sprites.h
const COLOR_SHORTCUTS = {
  '0,0,0': '___',
  '255,0,0': 'RED',
  '255,128,0': 'ORG',
  '255,255,0': 'YEL',
  '0,255,0': 'GRN',
  '0,255,255': 'CYN',
  '0,0,255': 'BLU',
  '128,0,255': 'PUR',
  '255,0,128': 'PNK',
  '255,255,255': 'WHT',
  '128,128,128': 'GRY',
  '64,64,64': 'DGR',
  '139,69,19': 'BRN',
  '255,200,150': 'SKN',
  '180,0,0': 'DRD',
  '0,180,0': 'LGR',
  '0,0,180': 'DBL',
  '249,56,1': 'GHO',
  '1,119,251': 'EYE',
  '244,59,2': 'SHY',
  '175,6,0': 'SHD'
};

// Paths relative to project root
const PROJECT_ROOT = path.join(__dirname, '..');
const SPRITES_PATH = path.join(PROJECT_ROOT, 'vizpow', 'emoji_sprites.h');
const WEBSERVER_PATH = path.join(PROJECT_ROOT, 'vizpow', 'web_server.h');

function rgbToCrgb(r, g, b) {
  const key = `${r},${g},${b}`;
  if (COLOR_SHORTCUTS[key]) {
    return COLOR_SHORTCUTS[key];
  }
  // Check for close matches (within 5 units)
  for (const [colorKey, shortcut] of Object.entries(COLOR_SHORTCUTS)) {
    const [cr, cg, cb] = colorKey.split(',').map(Number);
    if (Math.abs(r - cr) <= 5 && Math.abs(g - cg) <= 5 && Math.abs(b - cb) <= 5) {
      return shortcut;
    }
  }
  return `CRGB(${r},${g},${b})`;
}

function parseWledJson(jsonStr) {
  let data;
  try {
    data = JSON.parse(jsonStr);
  } catch (e) {
    throw new Error(`Invalid JSON: ${e.message}`);
  }

  let colors = [];
  if (data.seg && data.seg.i) {
    colors = data.seg.i;
  } else if (data.i) {
    colors = data.i;
  } else if (Array.isArray(data)) {
    colors = data;
  } else {
    throw new Error('No pixel data found in JSON. Expected seg.i, i, or flat array.');
  }

  if (colors.length < 192) { // 64 pixels * 3 RGB values
    throw new Error(`Expected 192 values (64 pixels), got ${colors.length}`);
  }

  // Convert flat RGB array to pixel array
  const pixels = [];
  for (let i = 0; i < 192; i += 3) {
    pixels.push([colors[i], colors[i + 1], colors[i + 2]]);
  }

  return pixels;
}

function generateIconCode(name, pixels) {
  const constName = 'ICON_' + name.toUpperCase().replace(/[^A-Z0-9]/g, '_');

  let code = `// ${name}\nconst CRGB ${constName}[64] = {\n`;

  for (let row = 0; row < 8; row++) {
    code += '  ';
    for (let col = 0; col < 8; col++) {
      const idx = row * 8 + col;
      const [r, g, b] = pixels[idx];
      code += rgbToCrgb(r, g, b);
      if (row < 7 || col < 7) code += ', ';
    }
    code += '\n';
  }

  code += '};\n';
  return { code, constName };
}

function updateEmojiSprites(iconName, pixels) {
  let content = fs.readFileSync(SPRITES_PATH, 'utf8');

  const { code, constName } = generateIconCode(iconName, pixels);

  // Check if icon already exists
  if (content.includes(constName)) {
    throw new Error(`Icon ${constName} already exists in emoji_sprites.h`);
  }

  // 1. Add icon definition before "// ==================== ICON COUNT ===================="
  const iconCountMarker = '// ==================== ICON COUNT ====================';
  if (!content.includes(iconCountMarker)) {
    throw new Error('Could not find ICON COUNT marker in emoji_sprites.h');
  }
  content = content.replace(iconCountMarker, `${code}\n${iconCountMarker}`);

  // 2. Update ICON_COUNT
  const countMatch = content.match(/#define ICON_COUNT (\d+)/);
  if (!countMatch) {
    throw new Error('Could not find ICON_COUNT in emoji_sprites.h');
  }
  const oldCount = parseInt(countMatch[1]);
  const newCount = oldCount + 1;
  content = content.replace(/#define ICON_COUNT \d+/, `#define ICON_COUNT ${newCount}`);

  // 3. Add to ALL_ICONS array (before the closing brace and semicolon)
  // Find the last icon in the array and add after it
  const allIconsMatch = content.match(/const CRGB\* ALL_ICONS\[ICON_COUNT\] = \{([^}]+)\}/);
  if (!allIconsMatch) {
    throw new Error('Could not find ALL_ICONS array in emoji_sprites.h');
  }
  const allIconsContent = allIconsMatch[1];
  const newAllIconsContent = allIconsContent.trimEnd() + `, ${constName}\n`;
  content = content.replace(
    /const CRGB\* ALL_ICONS\[ICON_COUNT\] = \{[^}]+\}/,
    `const CRGB* ALL_ICONS[ICON_COUNT] = {${newAllIconsContent}}`
  );

  // 4. Add to ICON_NAMES array
  // Format the display name (remove ICON_ prefix, use provided name)
  const displayName = iconName.replace(/\s+/g, '');
  const iconNamesMatch = content.match(/const char\* ICON_NAMES\[ICON_COUNT\] = \{([^}]+)\}/);
  if (!iconNamesMatch) {
    throw new Error('Could not find ICON_NAMES array in emoji_sprites.h');
  }
  const iconNamesContent = iconNamesMatch[1];
  const newIconNamesContent = iconNamesContent.trimEnd() + `, "${displayName}"\n`;
  content = content.replace(
    /const char\* ICON_NAMES\[ICON_COUNT\] = \{[^}]+\}/,
    `const char* ICON_NAMES[ICON_COUNT] = {${newIconNamesContent}}`
  );

  fs.writeFileSync(SPRITES_PATH, content);
  console.log(`Updated emoji_sprites.h:`);
  console.log(`  - Added ${constName} definition`);
  console.log(`  - Updated ICON_COUNT: ${oldCount} -> ${newCount}`);
  console.log(`  - Added to ALL_ICONS array`);
  console.log(`  - Added "${displayName}" to ICON_NAMES array`);

  return { constName, displayName, newCount };
}

function updateWebServer(displayName) {
  let content = fs.readFileSync(WEBSERVER_PATH, 'utf8');

  // Find the emojiNames array in JavaScript
  const emojiNamesMatch = content.match(/const emojiNames = \[([^\]]+)\]/);
  if (!emojiNamesMatch) {
    throw new Error('Could not find emojiNames array in web_server.h');
  }

  // Check if already exists
  if (content.includes(`"${displayName}"`)) {
    console.log(`Warning: "${displayName}" may already exist in web_server.h emojiNames`);
  }

  const emojiNamesContent = emojiNamesMatch[1];
  const newEmojiNamesContent = emojiNamesContent.trimEnd() + `, "${displayName}"`;
  content = content.replace(
    /const emojiNames = \[[^\]]+\]/,
    `const emojiNames = [${newEmojiNamesContent}]`
  );

  // Update the comment with icon count
  content = content.replace(
    /\/\/ Icon names matching emoji_sprites\.h \(\d+ icons\)/,
    (match) => {
      const countMatch = match.match(/\((\d+) icons\)/);
      if (countMatch) {
        const newCount = parseInt(countMatch[1]) + 1;
        return `// Icon names matching emoji_sprites.h (${newCount} icons)`;
      }
      return match;
    }
  );

  fs.writeFileSync(WEBSERVER_PATH, content);
  console.log(`Updated web_server.h:`);
  console.log(`  - Added "${displayName}" to emojiNames array`);
}

function main() {
  const args = process.argv.slice(2);

  if (args.length < 1) {
    console.log(`
Usage: node add-icon.js <name> [wled-json-or-file]

Examples:
  node add-icon.js "Shy Guy"              # reads JSON from clipboard (macOS)
  node add-icon.js "Mario" ./mario.json   # reads JSON from file

The WLED JSON should contain 64 pixels (192 RGB values) in one of these formats:
  - {"seg":{"i":[r,g,b,r,g,b,...]}}
  - {"i":[r,g,b,r,g,b,...]}
  - [r,g,b,r,g,b,...]
`);
    process.exit(1);
  }

  const iconName = args[0];
  let jsonInput = args[1];

  // If no JSON provided, try to read from clipboard (macOS)
  if (!jsonInput) {
    try {
      const { execSync } = require('child_process');
      jsonInput = execSync('pbpaste', { encoding: 'utf8' });
      console.log('Read JSON from clipboard');
    } catch (e) {
      console.error('No JSON provided and could not read from clipboard.');
      console.error('Provide JSON as second argument or copy it to clipboard first.');
      process.exit(1);
    }
  }

  // Check if it's a file path
  if (fs.existsSync(jsonInput)) {
    jsonInput = fs.readFileSync(jsonInput, 'utf8');
    console.log(`Read JSON from file: ${args[1]}`);
  }

  console.log(`\nAdding icon: "${iconName}"\n`);

  try {
    // Parse the WLED JSON
    const pixels = parseWledJson(jsonInput);
    console.log(`Parsed ${pixels.length} pixels from JSON\n`);

    // Update emoji_sprites.h
    const { displayName } = updateEmojiSprites(iconName, pixels);
    console.log('');

    // Update web_server.h
    updateWebServer(displayName);

    console.log(`\nDone! Icon "${iconName}" has been added.`);
    console.log('Remember to recompile and upload to your ESP32.');

  } catch (e) {
    console.error(`Error: ${e.message}`);
    process.exit(1);
  }
}

main();
