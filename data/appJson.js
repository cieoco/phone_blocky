/********************************************
 * (4) parseWorkspaceToIR(workspace)
 *     把「workspace 裡的積木」轉成 IR Node 陣列
 ********************************************/
function parseWorkspaceToIR(workspace) {
    const topBlocks = workspace.getTopBlocks(true);
    const irNodes = [];
    // 沒接在 setup／loop 裡的積木不會執行；只計數（不翻譯，避免半成品積木擋住整支程式）
    let strayBlocks = 0;
  
    topBlocks.forEach(block => {
      // 如果該積木有父節點，則跳過，避免重複解析（因為它會由父積木解析）
      if (block.getParent()) return;
  
      let current = block;
      while (current) {
        if (typeof current.isEnabled === "function" && !current.isEnabled()) {
          // 被停用的積木不執行（Blockly 的慣例）
        } else if (current.type === "arduino_setup" || current.type === "arduino_loop" ||
            current.type.startsWith("procedures_def")) {
          // setup／loop 與副程式定義都在最外層；有回傳值的副程式由 getTranslator 明確報錯
          const node = getTranslator(current.type)(current);
          if (node) irNodes.push(node);
        } else {
          strayBlocks++;
        }
        current = current.getNextBlock();
      }
    });
    irNodes.strayBlocks = strayBlocks;
    return irNodes;
  }
  
 
  

  function parseBlockChain(block) {
    const nodes = [];
    let current = block;
    // 只解析線性連結，不遍歷 inputList（由控制積木自己處理）
    while (current) {
      // 被停用的積木不執行；不認得的積木由 getTranslator 回傳的函式直接報錯，不會默默略過
      if (typeof current.isEnabled !== "function" || current.isEnabled()) {
        const node = getTranslator(current.type)(current);
        if (node) {
          nodes.push(node);
        }
      }
      current = current.getNextBlock();
    }
    return nodes;
  }
  


/********************************************
 * (5) collectFunctionDeclarations(workspace)
 *     蒐集「宣告對外功能」積木，拉高到 PROG JSON 的頂層 `functions`
 *
 * 為什麼要拉高，而不是讓它留在 setup 裡由韌體執行：
 * 主機一套用新程式就要能讀到功能表（0x64），不能等 setup 跑完才成立——
 * setup 裡可能有 delay，主機會在這段空窗讀到舊表或空表。
 * 宣告積木仍然留在 setup 指令陣列裡，韌體會直接跳過（未知命令 -> continue）。
 ********************************************/
function collectFunctionDeclarations(workspace) {
    const byIdx = new Map();
    const warnings = [];

    workspace.getAllBlocks(false).forEach(block => {
        if (block.type !== 'blockly_func_declare') return;
        if (block.isInsertionMarker && block.isInsertionMarker()) return;
        // 停用的積木不算數（新舊 Blockly API 都顧到）
        const enabled = (typeof block.isEnabled === 'function') ? block.isEnabled() : !block.disabled;
        if (!enabled) return;

        const idx = Number(block.getFieldValue('IDX'));
        if (!Number.isInteger(idx) || idx < 0 || idx > 15) return;

        if (byIdx.has(idx)) {
            // 同一個編號宣告兩次：沿用先出現的那個，並明確告知。
            // 靜默覆蓋會讓學生看到「我明明設成類比」卻拿到數位元件。
            warnings.push(`編號 ${idx} 被宣告了不只一次，採用最先出現的那個`);
            return;
        }
        byIdx.set(idx, {
            idx: idx,
            analog: block.getFieldValue('KIND') === 'ANALOG',
            readable: block.getFieldValue('READABLE') === 'TRUE'
        });
    });

    const declarations = [...byIdx.values()].sort((a, b) => a.idx - b.idx);

    // 契約要求編號由 0 起連續（韌體的 n_func 是「數量」，中間空號會讓
    // 主機生出一個對應不到任何積木的元件）。
    declarations.forEach((d, i) => {
        if (d.idx !== i) {
            warnings.push(`編號不連續：預期 ${i} 卻是 ${d.idx}，主機會多生出用不到的元件`);
        }
    });

    return { declarations, warnings };
}
