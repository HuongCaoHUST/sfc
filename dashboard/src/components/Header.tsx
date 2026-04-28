import React from 'react';
import { Camera, Settings } from 'lucide-react';

const Header = () => {
  return (
    <header className="h-16 md:h-20 flex items-center justify-between bg-[#111] px-4 md:px-6 border-b border-[#222] shrink-0">
      <div className="flex items-center gap-3">
        <div className="p-2 bg-blue-500/10 rounded-lg hidden sm:block">
          <Camera className="w-5 h-5 md:w-6 md:h-6 text-blue-500" />
        </div>
        <div>
          <h1 className="text-sm md:text-xl font-bold tracking-tight">AI CAMERA</h1>
        </div>
      </div>
      <Settings className="w-5 h-5 text-gray-600 cursor-pointer hover:text-white transition-colors" />
    </header>
  );
};

export default Header;
