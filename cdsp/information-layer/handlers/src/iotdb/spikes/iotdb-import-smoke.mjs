async function tryImport(name) {
  try {
    const mod = await import(name);
    console.log(`[smoke] ✅ imported ${name}`);
    console.log(`[smoke] keys:`, Object.keys(mod));
    return true;
  } catch (e) {
    console.log(`[smoke] ❌ failed ${name}: ${e.message}`);
    return false;
  }
}

const a = await tryImport("@iotdb/client");
const b = await tryImport("iotdb-client-nodejs");

if (!a && !b) {
  process.exit(1);
}
