import React from 'react';

const Footer = () => {
  return (
    <footer className="h-8 bg-[#0a0a0a] border-t border-[#222] flex items-center justify-between px-4 shrink-0 hidden md:flex">
      <div className="flex gap-4">
        <span className="text-[9px] text-gray-600 font-mono">CPU: 12%</span>
        <span className="text-[9px] text-gray-600 font-mono">MEM: 1.2GB</span>
        <span className="text-[9px] text-gray-600 font-mono">NET: Real-time</span>
      </div>
      <div className="text-[9px] text-gray-600 font-mono uppercase tracking-widest">Secure AI Node v2.4.0-Stable</div>
    </footer>
  );
};

export default Footer;
