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
  