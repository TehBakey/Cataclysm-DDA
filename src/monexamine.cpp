#include "monexamine.h"

#include <climits>
#include <string>
#include <utility>
#include <list>
#include <map>
#include <memory>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "creature_tracker.h"
#include "game.h"
#include "game_inventory.h"
#include "handle_liquid.h"
#include "item.h"
#include "iuse.h"
#include "map.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "output.h"
#include "string_input_popup.h"
#include "translations.h"
#include "ui.h"
#include "bodypart.h"
#include "debug.h"
#include "enums.h"
#include "player_activity.h"
#include "rng.h"
#include "string_formatter.h"
#include "units.h"
#include "type_id.h"
#include "pimpl.h"
#include "point.h"

const species_id ZOMBIE( "ZOMBIE" );

const efftype_id effect_controlled( "controlled" );
const efftype_id effect_harnessed( "harnessed" );
const efftype_id effect_has_bag( "has_bag" );
const efftype_id effect_milked( "milked" );
const efftype_id effect_monster_armor( "monster_armor" );
const efftype_id effect_tied( "tied" );
const efftype_id effect_riding( "riding" );
const efftype_id effect_ridden( "ridden" );
const efftype_id effect_saddled( "monster_saddled" );
const skill_id skill_survival( "survival" );

bool monexamine::pet_menu( monster &z )
{
    enum choices {
        swap_pos = 0,
        push_zlave,
        rename,
        attach_bag,
        drop_all,
        give_items,
        mon_armor_add,
        mon_harness_remove,
        mon_armor_remove,
        play_with_pet,
        pheromone,
        milk,
        attach_saddle,
        remove_saddle,
        mount,
        rope
    };

    uilist amenu;
    std::string pet_name = z.get_name();
    bool is_zombie = z.type->in_species( ZOMBIE );
    if( is_zombie ) {
        pet_name = _( "zombie slave" );
    }

    amenu.text = string_format( _( "What to do with your %s?" ), pet_name );

    amenu.addentry( swap_pos, true, 's', _( "Swap positions" ) );
    amenu.addentry( push_zlave, true, 'p', _( "Push %s" ), pet_name );
    amenu.addentry( rename, true, 'e', _( "Rename" ) );
    if( z.has_effect( effect_has_bag ) || z.has_effect( effect_monster_armor ) ) {
        amenu.addentry( give_items, true, 'g', _( "Place items into bag" ) );
        amenu.addentry( drop_all, true, 'd', _( "Drop all items except armor" ) );
    }
    if( !z.has_effect( effect_has_bag ) ) {
        amenu.addentry( attach_bag, true, 'b', _( "Attach bag" ) );
    }
    if( z.has_effect( effect_harnessed ) ) {
        amenu.addentry( mon_harness_remove, true, 'H', _( "Remove vehicle harness from %s" ), pet_name );
    }
    if( z.has_effect( effect_monster_armor ) ) {
        amenu.addentry( mon_armor_remove, true, 'a', _( "Remove armor from %s" ), pet_name );
    } else {
        amenu.addentry( mon_armor_add, true, 'a', _( "Equip %s with armor" ), pet_name );
    }
    if( z.has_flag( MF_BIRDFOOD ) || z.has_flag( MF_CATFOOD ) || z.has_flag( MF_DOGFOOD ) ||
        z.has_flag( MF_CANPLAY ) ) {
        amenu.addentry( play_with_pet, true, 'y', _( "Play with %s" ), pet_name );
    }
    if( z.has_effect( effect_tied ) ) {
        amenu.addentry( rope, true, 't', _( "Untie" ) );
    } else {
        std::vector<item *> rope_inv = g->u.items_with( []( const item & itm ) {
            return itm.has_flag( "TIE_UP" );
        } );
        if( !rope_inv.empty() ) {
            amenu.addentry( rope, true, 't', _( "Tie" ) );
        } else {
            amenu.addentry( rope, false, 't', _( "You need any type of rope to tie %s in place" ),
                            pet_name );
        }
    }
    if( is_zombie ) {
        amenu.addentry( pheromone, true, 'z', _( "Tear out pheromone ball" ) );
    }

    if( z.has_flag( MF_MILKABLE ) ) {
        amenu.addentry( milk, true, 'm', _( "Milk %s" ), pet_name );
    }
    if( z.has_flag( MF_PET_MOUNTABLE ) && !z.has_effect( effect_saddled ) &&
        g->u.has_amount( "riding_saddle", 1 ) && g->u.get_skill_level( skill_survival ) >= 1 ) {
        amenu.addentry( attach_saddle, true, 'h', _( "Attach a saddle to %s" ), pet_name );
    } else if( z.has_flag( MF_PET_MOUNTABLE ) && z.has_effect( effect_saddled ) ) {
        amenu.addentry( remove_saddle, true, 'h', _( "Remove the saddle from %s" ), pet_name );
    } else if( z.has_flag( MF_PET_MOUNTABLE ) && !z.has_effect( effect_saddled ) &&
               g->u.has_amount( "riding_saddle", 1 ) && g->u.get_skill_level( skill_survival ) < 1 ) {
        amenu.addentry( remove_saddle, false, 'h', _( "You don't know how to saddle %s" ), pet_name );
    }
    if( z.has_flag( MF_PET_MOUNTABLE ) && ( ( z.has_effect( effect_saddled ) &&
                                            g->u.get_skill_level( skill_survival ) >= 1 ) || g->u.get_skill_level( skill_survival ) >= 4 ) &&
        z.get_size() >= ( g->u.get_size() + 1 ) && g->u.get_weight() <= z.get_weight() / 5 ) {
        amenu.addentry( mount, true, 'r', _( "Mount %s" ), pet_name );
    } else if( !z.has_flag( MF_PET_MOUNTABLE ) ) {
        amenu.addentry( mount, false, 'r', _( "%s cannot be mounted" ), pet_name );
    } else if( z.get_size() <= g->u.get_size() ) {
        amenu.addentry( mount, false, 'r', _( "%s is too small to carry your weight" ), pet_name );
    } else if( g->u.get_skill_level( skill_survival ) < 1 ) {
        amenu.addentry( mount, false, 'r', _( "You have no knowledge of riding at all" ) );
    } else if( g->u.get_weight() >= z.get_weight() / 5 ) {
        amenu.addentry( mount, false, 'r', _( "You are too heavy to mount %s" ), pet_name );
    } else if( !z.has_effect( effect_saddled ) && g->u.get_skill_level( skill_survival ) < 4 ) {
        amenu.addentry( mount, false, 'r', _( "You are not skilled enough to ride without a saddle" ) );
    } else if( z.has_effect( effect_saddled ) && g->u.get_skill_level( skill_survival ) < 1 ) {
        amenu.addentry( mount, false, 'r', _( "Despite the saddle, you still don't know how to ride %s" ),
                        pet_name );
    }
    amenu.query();
    int choice = amenu.ret;

    switch( choice ) {
        case swap_pos:
            swap( z );
            break;
        case push_zlave:
            push( z );
            break;
        case rename:
            rename_pet( z );
            break;
        case attach_bag:
            attach_bag_to( z );
            break;
        case drop_all:
            dump_items( z );
            break;
        case give_items:
            return give_items_to( z );
        case mon_armor_add:
            return add_armor( z );
        case mon_harness_remove:
            remove_harness( z );
            break;
        case mon_armor_remove:
            remove_armor( z );
            break;
        case play_with_pet:
            if( query_yn( _( "Spend a few minutes to play with your %s?" ), pet_name ) ) {
                play_with( z );
            }
            break;
        case pheromone:
            if( query_yn( _( "Really kill the zombie slave?" ) ) ) {
                kill_zslave( z );
            }
            break;
        case rope:
            tie_or_untie( z );
            break;
        case attach_saddle:
        case remove_saddle:
            attach_or_remove_saddle( z );
            break;
        case mount:
            mount_pet( z );
            break;
        case milk:
            milk_source( z );
            break;
        default:
            break;
    }
    return true;
}

int monexamine::pet_armor_pos( monster &z )
{
    int pos = g->inv_for_filter( _( "Pet armor" ), [z]( const item & it ) {
        return z.type->bodytype == it.get_pet_armor_bodytype() &&
               z.get_volume() >= it.get_pet_armor_min_vol() &&
               z.get_volume() <= it.get_pet_armor_max_vol();
    } );
    return pos;
}

void monexamine::attach_or_remove_saddle( monster &z )
{
    if( z.has_effect( effect_saddled ) ) {
        z.remove_effect( effect_saddled );
        item riding_saddle( "riding_saddle", 0 );
        g->u.i_add( riding_saddle );
    } else {
        z.add_effect( effect_saddled, 1_turns, num_bp, true );
        g->u.use_amount( "riding_saddle", 1 );
    }
}

void monexamine::mount_pet( monster &z )
{
    g->u.add_effect( effect_riding, 1_turns, num_bp, true );
    z.add_effect( effect_ridden, 1_turns, num_bp, true );
    if( z.has_effect( effect_tied ) ) {
        z.remove_effect( effect_tied );
        if( z.tied_item ) {
            g->u.i_add( *z.tied_item, 0 );
            z.tied_item = cata::nullopt;
        }
    }
    if( z.has_effect( effect_harnessed ) ) {
        z.remove_effect( effect_harnessed );
        add_msg( m_info, _( "You remove the %s's harness." ), z.get_name() );
    }
    tripoint pnt = z.pos();
    auto mons = g->critter_tracker->find( pnt );
    if( mons == nullptr ) {
        add_msg( m_debug, "mount_pet() : monster not found in critter_tracker" );
        return;
    }
    g->u.mounted_creature = mons;
    if( g->u.is_hauling() ) {
        g->u.stop_hauling();
    }
    if( g->u.get_grab_type() != OBJECT_NONE ) {
        add_msg( m_warning, _( "You let go of the grabbed object." ) );
        g->u.grab( OBJECT_NONE );
    }
    g->place_player( pnt );
    z.facing = g->u.facing;
    add_msg( m_good, _( "You mount your steed." ) );
    g->u.mod_moves( -100 );
}

void monexamine::swap( monster &z )
{
    std::string pet_name = z.get_name();
    g->u.moves -= 150;

    ///\EFFECT_STR increases chance to successfully swap positions with your pet
    ///\EFFECT_DEX increases chance to successfully swap positions with your pet
    if( !one_in( ( g->u.str_cur + g->u.dex_cur ) / 6 ) ) {
        bool t = z.has_effect( effect_tied );
        if( t ) {
            z.remove_effect( effect_tied );
        }

        tripoint zp = z.pos();
        z.move_to( g->u.pos(), true );
        g->u.setpos( zp );

        if( t ) {
            z.add_effect( effect_tied, 1_turns, num_bp, true );
        }
        add_msg( _( "You swap positions with your %s." ), pet_name );
    } else {
        add_msg( _( "You fail to budge your %s!" ), pet_name );
    }
}

void monexamine::push( monster &z )
{
    std::string pet_name = z.get_name();
    g->u.moves -= 30;

    ///\EFFECT_STR increases chance to successfully push your pet
    if( !one_in( g->u.str_cur ) ) {
        add_msg( _( "You pushed the %s." ), pet_name );
    } else {
        add_msg( _( "You pushed the %s, but it resisted." ), pet_name );
        return;
    }

    int deltax = z.posx() - g->u.posx(), deltay = z.posy() - g->u.posy();
    z.move_to( tripoint( z.posx() + deltax, z.posy() + deltay, z.posz() ) );
}

void monexamine::rename_pet( monster &z )
{
    std::string unique_name = string_input_popup()
                              .title( _( "Enter new pet name:" ) )
                              .width( 20 )
                              .query_string();
    if( unique_name.length() > 0 ) {
        z.unique_name = unique_name;
    }
}

void monexamine::attach_bag_to( monster &z )
{
    std::string pet_name = z.get_name();
    int pos = g->inv_for_filter( _( "Bag item" ), []( const item & it ) {
        return it.is_armor() && it.get_storage() > 0_ml;
    } );

    if( pos == INT_MIN ) {
        add_msg( _( "Never mind." ) );
        return;
    }

    item &it = g->u.i_at( pos );
    // force it to the front of the monster's inventory in case they have armor on
    z.inv.insert( z.inv.begin(), it );
    add_msg( _( "You mount the %1$s on your %2$s, ready to store gear." ),
             it.display_name(), pet_name );
    g->u.i_rem( pos );
    z.add_effect( effect_has_bag, 1_turns, num_bp, true );
    g->u.moves -= 200;
}

void monexamine::dump_items( monster &z )
{
    std::string pet_name = z.get_name();
    int armor_index = 0;
    bool found_armor = false;
    for( auto &it : z.inv ) {
        if( z.has_effect( effect_monster_armor ) && it.is_pet_armor( true ) ) {
            found_armor = true;
        } else {
            armor_index += 1;
            g->m.add_item_or_charges( z.pos(), it );
        }
    }
    item armor;
    if( found_armor ) {
        armor = z.inv[ armor_index ];
    }

    z.inv.clear();
    z.remove_effect( effect_has_bag );
    if( found_armor ) {
        armor.set_var( "pet_armor", "true" );
        z.add_item( armor );
    }
    add_msg( _( "You dump the contents of the %s's bag on the ground." ), pet_name );
    g->u.moves -= 200;
}

bool monexamine::give_items_to( monster &z )
{
    std::string pet_name = z.get_name();
    if( z.inv.empty() ) {
        add_msg( _( "There is no container on your %s to put things in!" ), pet_name );
        return true;
    }

    int armor_index = INT_MIN;
    if( z.has_effect( effect_monster_armor ) ) {
        armor_index = 0;
        for( auto &it : z.inv ) {
            if( it.is_pet_armor( true ) ) {
                break;
            } else {
                armor_index += 1;
            }
        }
    }

    // might be a bag, might be armor
    item &storage = z.inv[0];
    units::volume max_cap = storage.get_storage();
    units::mass max_weight = z.weight_capacity() - storage.weight();
    if( armor_index != 0 && armor_index != INT_MIN ) {
        item &armor = z.inv[armor_index];
        max_cap += armor.get_storage() + armor.volume();
        max_weight -= armor.weight();
    }

    if( z.inv.size() > 1 ) {
        for( auto &i : z.inv ) {
            max_cap -= i.volume();
            max_weight -= i.weight();
        }
    }

    if( max_weight <= 0_gram ) {
        add_msg( _( "%1$s is overburdened. You can't transfer your %2$s." ),
                 pet_name, storage.tname( 1 ) );
        return true;
    }
    if( max_cap <= 0_ml ) {
        add_msg( _( "There's no room in your %1$s's %2$s for that, it's too bulky!" ),
                 pet_name, storage.tname( 1 ) );
        return true;
    }

    const auto items_to_stash = game_menus::inv::multidrop( g->u );
    if( !items_to_stash.empty() ) {
        g->u.drop( items_to_stash, z.pos(), true );
        z.add_effect( effect_controlled, 5_turns );
        return true;
    }
    return false;
}

bool monexamine::add_armor( monster &z )
{
    std::string pet_name = z.get_name();
    int pos = pet_armor_pos( z );

    if( pos == INT_MIN ) {
        add_msg( _( "Never mind." ) );
        return true;
    }

    item &armor = g->u.i_at( pos );
    units::mass max_weight = z.weight_capacity();
    for( auto &i : z.inv ) {
        max_weight -= i.weight();
    }
    if( max_weight <= 0_gram ) {
        add_msg( _( "Your %1$s is too heavy for your %2$s." ), armor.tname( 1 ), pet_name );
        return true;
    }

    armor.set_var( "pet_armor", "true" );
    z.add_item( armor );
    add_msg( _( "You put the %1$s on your %2$s, protecting it from future harm." ),
             armor.display_name(), pet_name );
    g->u.i_rem( pos );
    z.add_effect( effect_monster_armor, 1_turns, num_bp, true );
    g->u.moves -= 200;
    return true;
}

void monexamine::remove_harness( monster &z )
{
    z.remove_effect( effect_harnessed );
    add_msg( m_info, _( "You unhitch %s from the vehicle." ), z.get_name() );
}

void monexamine::remove_armor( monster &z )
{
    std::string pet_name = z.get_name();
    bool found_armor = false;
    int pos = 0;
    for( auto &it : z.inv ) {
        if( it.is_pet_armor( true ) ) {
            found_armor = true;
            it.erase_var( "pet_armor" );
            g->m.add_item_or_charges( z.pos(), it );
            add_msg( m_info, "You remove the %1$s from %2$s.", it.tname( 1 ), pet_name );
            z.inv.erase( z.inv.begin() + pos );
            g->u.moves -= 200;
            break;
        } else {
            pos += 1;
        }
    }
    if( !found_armor ) {
        add_msg( m_bad, _( "Your %1$s isn't wearing armor!" ), pet_name );
    }
    z.remove_effect( effect_monster_armor );
}

void monexamine::play_with( monster &z )
{
    std::string pet_name = z.get_name();
    g->u.assign_activity( activity_id( "ACT_PLAY_WITH_PET" ), rng( 50, 125 ) * 100 );
    g->u.activity.str_values.push_back( pet_name );
}

void monexamine::kill_zslave( monster &z )
{
    z.apply_damage( &g->u, bp_torso, 100 ); // damage the monster (and its corpse)
    z.die( &g->u ); // and make sure it's really dead

    g->u.moves -= 150;

    if( !one_in( 3 ) ) {
        g->u.add_msg_if_player( _( "You tear out the pheromone ball from the zombie slave." ) );
        item ball( "pheromone", 0 );
        iuse pheromone;
        pheromone.pheromone( &g->u, &ball, true, g->u.pos() );
    }
}

void monexamine::tie_or_untie( monster &z )
{
    if( z.has_effect( effect_tied ) ) {
        z.remove_effect( effect_tied );
        if( z.tied_item ) {
            g->u.i_add( *z.tied_item, 0 );
            z.tied_item = cata::nullopt;
        }
    } else {
        std::vector<item *> rope_inv = g->u.items_with( []( const item & itm ) {
            return itm.has_flag( "TIE_UP" );
        } );
        if( rope_inv.empty() ) {
            return;
        }
        int i = 0;
        uilist selection_menu;
        selection_menu.text = string_format( _( "Select an item to tie your %s with." ), z.get_name() );
        selection_menu.addentry( i++, true, MENU_AUTOASSIGN, _( "Cancel" ) );
        for( auto iter : rope_inv ) {
            selection_menu.addentry( i++, true, MENU_AUTOASSIGN, _( "Use %s" ), iter->tname() );
        }
        selection_menu.selected = 1;
        selection_menu.query();
        auto index = selection_menu.ret;
        if( index == 0 || index == UILIST_CANCEL || index < 0 ||
            index > static_cast<int>( rope_inv.size() ) ) {
            return;
        }
        auto rope_item = rope_inv[index - 1];
        int item_pos = g->u.get_item_position( rope_item );
        if( item_pos != INT_MIN ) {
            z.tied_item = *rope_item;
            g->u.i_rem( item_pos );
            z.add_effect( effect_tied, 1_turns, num_bp, true );
        }
    }
}

void monexamine::milk_source( monster &source_mon )
{
    const auto milked_item = source_mon.type->starting_ammo.find( "milk" );
    if( milked_item == source_mon.type->starting_ammo.end() ) {
        debugmsg( "%s is milkable but has no milk in its starting ammo!",
                  source_mon.get_name() );
        return;
    }
    const int milk_per_day = milked_item->second;
    const time_duration milking_freq = 1_days / milk_per_day;

    int remaining_milk = milk_per_day;
    if( source_mon.has_effect( effect_milked ) ) {
        remaining_milk -= source_mon.get_effect_dur( effect_milked ) / milking_freq;
    }

    if( remaining_milk > 0 ) {
        // pin the cow in place if it isn't already
        bool temp_tie = !source_mon.has_effect( effect_tied );
        if( temp_tie ) {
            source_mon.add_effect( effect_tied, 1_turns, num_bp, true );
        }

        item milk( milked_item->first, calendar::turn, remaining_milk );
        milk.set_item_temperature( 311.75 );
        if( liquid_handler::handle_liquid( milk, nullptr, 1, nullptr, nullptr, -1, &source_mon ) ) {
            add_msg( _( "You milk the %s." ), source_mon.get_name() );
            int transferred_milk = remaining_milk - milk.charges;
            source_mon.add_effect( effect_milked, milking_freq * transferred_milk );
            g->u.mod_moves( -to_moves<int>( transferred_milk * 1_minutes / 5 ) );
        }
        if( temp_tie ) {
            source_mon.remove_effect( effect_tied );
        }
    } else {
        add_msg( _( "The %s's udders run dry." ), source_mon.get_name() );
    }
}
