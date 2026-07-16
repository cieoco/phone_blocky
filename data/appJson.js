/********************************************
 * (4) parseWorkspaceToIR(workspace)
 *     把「workspace 裡的積木」轉成 IR Node 陣列
 ********************************************/
function parseWorkspaceToIR(workspace) {
    const topBlocks = workspace.getTopBlocks(true);
    const irNodes = [];
  
    topBlocks.forEach(block => {
      // 如果該積木有父節點，則跳過，避免重複解析（因為它會由父積木解析）
      if (block.getParent()) return;
  
      let current = block;
      while (current) {
        const translator = getTranslator(current.type);
        if (translator) {
          const node = translator(current);
          if (node) {
            irNodes.push(node);
          }
        } else {
          console.warn("沒有對應的翻譯器:", current.type);
        }
        current = current.getNextBlock();
      }
    });
    return irNodes;
  }
  
 
  

  function parseBlockChain(block) {
    const nodes = [];
    let current = block;
    // 只解析線性連結，不遍歷 inputList（由控制積木自己處理）
    while (current) {
      const translator = getTranslator(current.type);
      if (translator) {
        const node = translator(current);
        if (node) {
          nodes.push(node);
        }
      } else {
        console.warn("沒有對應的翻譯器:", current.type);
      }
      current = current.getNextBlock();
    }
    return nodes;
  }
  